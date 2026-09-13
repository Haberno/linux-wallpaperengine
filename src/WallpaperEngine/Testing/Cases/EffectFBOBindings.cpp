#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Render/Objects/Effects/EffectFBOBindings.h"

using WallpaperEngine::Render::Objects::Effects::EffectFBOBindings;

TEST_CASE ("effect swaps transpose current references with fixed authored endpoints", "[effect][swap]") {
    EffectFBOBindings bindings ({ "A", "B", "C" });
    bindings.swap ("A", "B");
    CHECK (bindings.resolveName ("A") == "B");
    CHECK (bindings.resolveName ("B") == "A");
    CHECK (bindings.resolveName ("C") == "C");

    bindings.swap ("B", "C");
    CHECK (bindings.resolveName ("A") == "C");
    CHECK (bindings.resolveName ("B") == "A");
    CHECK (bindings.resolveName ("C") == "B");
}

TEST_CASE ("effect swaps preserve odd and even history across frame boundaries", "[effect][swap]") {
    EffectFBOBindings bindings ({ "velocity1", "velocity2" });
    for (const auto& expected : { "velocity1", "velocity2", "velocity1", "velocity2" }) {
	// Resolve the early pass, then execute the authored tail swap for this frame.
	CHECK (bindings.resolveName ("velocity1") == expected);
	bindings.swap ("velocity1", "velocity2");
    }
    CHECK (bindings.resolveName ("velocity1") == "velocity1");
    CHECK (bindings.resolveName ("velocity2") == "velocity2");

    bindings.swap ("velocity1", "velocity2");
    bindings.swap ("velocity1", "velocity2");
    CHECK (bindings.resolveName ("velocity1") == "velocity1");
    CHECK (bindings.resolveName ("velocity2") == "velocity2");
}

TEST_CASE ("effect swaps ignore missing and self endpoints without disturbing prior swaps", "[effect][swap]") {
    EffectFBOBindings bindings ({ "A", "B" });
    bindings.swap ("A", "B");
    bindings.swap ("A", "missing");
    bindings.swap ("missing", "B");
    bindings.swap ("previous", "A");
    bindings.swap ("a", "A");
    bindings.swap ("A", "A");
    CHECK (bindings.resolveName ("A") == "B");
    CHECK (bindings.resolveName ("B") == "A");
    CHECK (bindings.resolveName ("missing") == "missing");
    CHECK (bindings.resolveName ("previous") == "previous");

    EffectFBOBindings empty ({});
    empty.swap ("A", "B");
    CHECK (empty.resolveName ("A") == "A");
}

TEST_CASE ("effect swaps remain local when effect instances reuse FBO names", "[effect][swap]") {
    EffectFBOBindings first ({ "A", "B", "C" });
    EffectFBOBindings second ({ "A", "B", "C" });
    first.swap ("A", "B");
    second.swap ("B", "C");
    CHECK (first.resolveName ("A") == "B");
    CHECK (first.resolveName ("B") == "A");
    CHECK (first.resolveName ("C") == "C");
    CHECK (second.resolveName ("A") == "A");
    CHECK (second.resolveName ("B") == "C");
    CHECK (second.resolveName ("C") == "B");
}
