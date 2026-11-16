// Test build only (TEST=1 ./build.sh): commands from <game>\FrontierDog.test, one per line, read once a second; the
// file is deleted after reading. Results go to FrontierDog.log with the prefix "test:".
#pragma once
namespace fd::testhooks {
void tick();
}
