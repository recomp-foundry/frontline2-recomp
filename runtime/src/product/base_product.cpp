#include "runtime_product.h"

namespace RuntimeProduct {

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor{
        Kind::BaseGame,
        "FrontlineRecomp",
    };
    return descriptor;
}

} // namespace RuntimeProduct
