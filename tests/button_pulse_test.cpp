#include "button_pulse.h"
#include <cstdlib>
#include <iostream>

static void Check(bool ok, const char* message) {
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
  ButtonPulse pulse;
  pulse.Press(0x0010, 1000); // Start tapped between frames.
  Check(pulse.Read(1016) == 0x0010, "Connection query lost Start");
  Check(pulse.Read(1017) == 0x0010, "Button query lost Start after connection query");
  Check(pulse.Read(1049) == 0x0010, "Tap ended before the minimum pulse");
  Check(pulse.Read(1050) == 0, "Released tap remained stuck");
  pulse.Press(0x0010, 2000);
  pulse.Press(0x1000, 2020);
  Check(pulse.Read(2030) == 0x1010, "Overlapping taps lost a button");
  Check(pulse.Read(2051) == 0x1000, "Independent button expiry failed");
  pulse.Clear();
  Check(pulse.Read(2052) == 0, "Focus loss retained an input");
  std::cout << "PASS: repeated polling, pulse expiry, overlapping buttons, focus reset\n";
}
