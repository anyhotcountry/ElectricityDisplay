#include <stdio.h>
#include <time.h>

#include "common/cs_dbg.h"
#include "common/platform.h"
#include "fw/src/mgos_app.h"
#include "fw/src/mgos_sys_config.h"
#include "fw/src/mgos_wifi.h"

#include "mgos_mqtt.h"

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 *d1 = nullptr;
static int min = 0;
static int day = -1;
static uint16_t dayTotal = 0;
static ulong durationTotal = 0;
static ulong buckets[20];

static void sub(struct mg_connection *c, const char *fmt, ...)
{
  char buf[100];
  struct mg_mqtt_topic_expression te = {.topic = buf, .qos = 1};
  uint16_t sub_id = mgos_mqtt_get_packet_id();
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  mg_mqtt_subscribe(c, &te, 1, sub_id);
  LOG(LL_INFO, ("Subscribing to %s (id %u)", buf, sub_id));
}

static void show_watts(Adafruit_SSD1306 *d, const int watts, int pulses, int duration)
{
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  if (day != tm.tm_yday) {
    day = tm.tm_yday;
    min = watts;
    dayTotal = 0;
    durationTotal = 0;
    for (int i = 0; i < 20; i++)
    {
      buckets[i] = 0;
    }
  }

  min = watts < min ? watts : min;
  int bucket = watts / 100.0;
  bucket = bucket >= 20 ? 19 : bucket;
  buckets[bucket] += pulses;
  dayTotal += pulses;
  durationTotal += duration;
  uint16_t average = (uint16_t)(dayTotal * 3600000ul / durationTotal);
  double cost = 0.15e-3 * average * 24;
  uint8_t textSize = watts < 1000 ? 3 : 2;
  d->clearDisplay();
  d->setTextColor(WHITE);
  d->drawRect(0, 0, 64, 30, WHITE);
  d->setTextSize(textSize);
  d->setCursor(5, 5);
  d->printf("%d", watts);
  d->setTextSize(1);
  d->setCursor(0, 35);
  d->printf("Min: %d\n", min);
  d->printf("Tot: %d\n", dayTotal);
  d->printf("Avg: %d\n", average);
  d->printf("Cost: %0.2f\n", cost);
  d->printf("RX: %02d:%02d\n", tm.tm_hour, tm.tm_min);

  // Draw histogram
  int height = d->height() - d->getCursorY();
  for (int i = 0; i < 20; i++)
  {
    int barHeight = height * buckets[i] / dayTotal;
    d->drawRect(i * 3, 128 - barHeight, 3, barHeight, WHITE);
  }

  d->display();
}

static void show_text(Adafruit_SSD1306 *d, int x, int y, int size, const char *text)
{
  d->setTextSize(size);
  d->setCursor(x, y);
  d->printf(text);
  d->display();
}

static void ev_handler(struct mg_connection *c, int ev, void *p,
                       void *user_data)
{
  struct mg_mqtt_message *msg = (struct mg_mqtt_message *)p;

  if (ev == MG_EV_MQTT_CONNACK)
  {
    LOG(LL_INFO, ("CONNACK: %d", msg->connack_ret_code));
    if (get_cfg()->mqtt.sub == NULL)
    {
      LOG(LL_ERROR, ("Run 'mgos config-set mqtt.sub=...'"));
    }
    else
    {
      sub(c, "%s", get_cfg()->mqtt.sub);
    }
  }
  else if (ev == MG_EV_MQTT_SUBACK)
  {
    LOG(LL_INFO, ("Subscription %u acknowledged", msg->message_id));
  }
  else if (ev == MG_EV_MQTT_PUBLISH)
  {
    struct mg_str *s = &msg->payload;
    // struct json_token t = JSON_INVALID_TOKEN;
    // char buf[100];
    int duration, wattage, pulses;

    LOG(LL_INFO, ("got command: [%.*s]", (int)s->len, s->p));
    /* Our subscription is at QoS 1, we must acknowledge messages sent ot us. */
    mg_mqtt_puback(c, msg->message_id);
    if (json_scanf(s->p, s->len, "{duration: %d, wattage: %d, pulses: %d}", &duration,
                   &wattage, &pulses) == 3)
    {
      LOG(LL_INFO, ("Wattage: %d", wattage));
      show_watts(d1, wattage, pulses, duration);
    }
  }
  (void)user_data;
}

void setup(void)
{
  // I2C
  d1 = new Adafruit_SSD1306(4 /* RST GPIO */, Adafruit_SSD1306::RES_128_64);

  if (d1 != nullptr)
  {
    d1->begin(SSD1306_SWITCHCAPVCC, 0x3C);
    d1->setRotation(1);
    d1->clearDisplay();
    d1->setTextColor(WHITE);
    show_text(d1, 0, d1->height() / 4, 1, "Connecting");
  }

  mgos_mqtt_add_global_handler(ev_handler, NULL);
}

void loop(void)
{
  /* For now, do not use delay() inside loop, use timers instead. */
}
