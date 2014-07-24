#include "tea.h"

void setup() {
  Serial.begin(115200);
}

char buffy[100];

void loop() {
  int got;
  Serial.print(" > ");
  got = Serial.readBytes(buffy, sizeof(buffy)-1);
  if (got > 0) {
      buffy[got] = '\0';
      float a;
      a = tea_calc(buffy, NULL);
      Serial.println("");
      Serial.println(a);
  }
}

/* vim: set ai cin et sw=2 ts=2 : */
