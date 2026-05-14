#include "RouterPublishApi.h"
#include "OscRouter.h"

namespace pairdriver::oscrouter {

bool PublishOsc(const char *source_id,
                const char *address,
                const char *typetag,
                const void *args,
                size_t      arg_len)
{
    ::oscrouter::OscRouter *router =
        ::oscrouter::g_activeRouter.load(std::memory_order_acquire);
    if (!router) return false;
    return router->PublishOsc(source_id, address, typetag, args, arg_len);
}

} // namespace pairdriver::oscrouter
