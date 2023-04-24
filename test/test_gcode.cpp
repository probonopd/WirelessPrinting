#include <unity.h>
#include "GCode.h"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_M115ExtractString() {
  char got[16];
  memset(got, 0, 16);
  TEST_ASSERT_FALSE(M115ExtractString(got, "", "fieldname"));
  memset(got, 0, 16);
  TEST_ASSERT_TRUE(M115ExtractString(got, "asdf:ghjk", "asdf"));
  TEST_ASSERT_EQUAL_STRING("ghjk", got);
  memset(got, 0, 16);
  TEST_ASSERT_TRUE(M115ExtractString(got, "\n\nMACHINE_TYPE:foo OTHER_FIELD:bar", "MACHINE_TYPE"));
  TEST_ASSERT_EQUAL_STRING("foo", got);
  memset(got, 0, 16);
  TEST_ASSERT_TRUE(M115ExtractString(got, "\n\nA:5 MACHINE_TYPE:bar EXTRUDER_COUNT:1ok", "MACHINE_TYPE"));
  TEST_ASSERT_EQUAL_STRING("bar", got);
  memset(got, 0, 16);
  TEST_ASSERT_TRUE(M115ExtractString(got, "\n\nA:5 MACHINE_TYPE:baz\nEXTRUDER_COUNT:1ok", "MACHINE_TYPE"));
  TEST_ASSERT_EQUAL_STRING("baz", got);

}

void test_parseTemp() {
  float a, t;
  TEST_ASSERT_FALSE(parseTemp("invalid", "T0", &a, &t));

  const char* R = "ok T:32.8 /60.0 B:31.8 /30.0 T0:32.8 /0.0 @:0 B@:0";

  TEST_ASSERT_TRUE(parseTemp(R, "T", &a, &t));
  TEST_ASSERT_EQUAL(32.8, a);
  TEST_ASSERT_EQUAL(60.0, t);

  TEST_ASSERT_TRUE(parseTemp(R, "B", &a, &t));
  TEST_ASSERT_EQUAL(31.8, a);
  TEST_ASSERT_EQUAL(30.0, t);

  TEST_ASSERT_TRUE(parseTemp(R, "T0", &a, &t));
  TEST_ASSERT_EQUAL(32.8, a);
  TEST_ASSERT_EQUAL(0.0, t);
}

void test_parsePrusaHeatingTemp() {
  float a;
  TEST_ASSERT_FALSE(parsePrusaHeatingTemp("invalid", "T0", &a));
  const char* R = "ok T:32.8 E:0 B:31.8";
  TEST_ASSERT_TRUE(parsePrusaHeatingTemp(R, "T", &a));
  TEST_ASSERT_EQUAL(32.8, a);
  TEST_ASSERT_TRUE(parsePrusaHeatingTemp(R, "E", &a));
  TEST_ASSERT_EQUAL(0.0, a);
  TEST_ASSERT_TRUE(parsePrusaHeatingTemp(R, "B", &a));
  TEST_ASSERT_EQUAL(31.8, a);
}

int main( int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_M115ExtractString);
    RUN_TEST(test_parseTemp);
    RUN_TEST(test_parsePrusaHeatingTemp);
    UNITY_END();
}
