#include "Core/GammaEngine.h"

int main() {
    GammaEngine engine;
    if (engine.Initialize()) {
        engine.Run();
    }
    return 0;
}