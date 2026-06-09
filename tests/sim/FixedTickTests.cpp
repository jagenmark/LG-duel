#include "shared/Constants.hpp"
#include "shared/FixedTick.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
  int failures = 0;

  {
    float accumulator = 0.0F;
    const lg::FixedTickFrame frame = lg::planFixedTicks(
      accumulator,
      lg::kFixedTickSeconds * 3.5F,
      lg::kFixedTickSeconds,
      8
    );

    failures += expect(frame.tickCount == 3, "normal frame should schedule all available ticks");
    failures += expect(nearlyEqual(frame.droppedSeconds, 0.0F), "normal frame should not drop time");
    failures += expect(
      nearlyEqual(accumulator, lg::kFixedTickSeconds * 0.5F),
      "fixed tick planner should retain fractional time"
    );
  }

  {
    float accumulator = 0.0F;
    const lg::FixedTickFrame frame = lg::planFixedTicks(
      accumulator,
      lg::kFixedTickSeconds * 20.25F,
      lg::kFixedTickSeconds,
      8
    );

    failures += expect(frame.tickCount == 8, "overloaded frame should cap simulation work");
    failures += expect(
      nearlyEqual(frame.droppedSeconds, lg::kFixedTickSeconds * 12.0F),
      "overloaded frame should report dropped whole ticks"
    );
    failures += expect(
      nearlyEqual(accumulator, lg::kFixedTickSeconds * 0.25F),
      "overload handling should retain only fractional time"
    );
  }

  {
    float accumulator = lg::kFixedTickSeconds * 0.5F;
    const lg::FixedTickFrame frame = lg::planFixedTicks(
      accumulator,
      -1.0F,
      lg::kFixedTickSeconds,
      8
    );

    failures += expect(frame.tickCount == 0, "negative elapsed time should not schedule ticks");
    failures += expect(
      nearlyEqual(accumulator, lg::kFixedTickSeconds * 0.5F),
      "negative elapsed time should not change accumulator"
    );
  }

  return failures == 0 ? 0 : 1;
}
