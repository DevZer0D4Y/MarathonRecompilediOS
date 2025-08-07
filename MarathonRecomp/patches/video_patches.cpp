#include <api/Marathon.h>
#include <user/config.h>

void SetMSAALevel(PPCRegister& val)
{
    val.u32 = 0;
}

float ReflectionScaleFactor(EReflectionResolution ref) {
    switch (ref) {
        case EReflectionResolution::Eighth:
            return 0.5f;
        case EReflectionResolution::Quarter:
            return 1.0f;
        case EReflectionResolution::Half:
            return 2.0f;
        case EReflectionResolution::Full:
            return 4.0f;
        default:
            return 1.0f;
    }
}

// CreateTexture
PPC_FUNC_IMPL(__imp__sub_82619D00);
PPC_FUNC(sub_82619D00)
{
    auto pName = (stdx::string*)g_memory.Translate(ctx.r4.u32);

    if (*pName == "reflection0")
    {
        ctx.r5.u32 = static_cast<int>(static_cast<float>(ctx.r5.u32) *
            ReflectionScaleFactor(Config::ReflectionResolution));
        ctx.r6.u32 = static_cast<int>(static_cast<float>(ctx.r6.u32) *
            ReflectionScaleFactor(Config::ReflectionResolution));
    }

    __imp__sub_82619D00(ctx, base);
}

// CreateDepthStencilTexture
PPC_FUNC_IMPL(__imp__sub_82619B88);
PPC_FUNC(sub_82619B88)
{
    auto pName = (stdx::string*)g_memory.Translate(ctx.r4.u32);

    if (*pName == "depthstencil_1_4")
    {
        ctx.r5.u32 = static_cast<int>(static_cast<float>(ctx.r5.u32) *
            ReflectionScaleFactor(Config::ReflectionResolution));
        ctx.r6.u32 = static_cast<int>(static_cast<float>(ctx.r6.u32) *
            ReflectionScaleFactor(Config::ReflectionResolution));

        // Bad hack to stop EDRAM cache from messing up
        if (Config::ReflectionResolution == EReflectionResolution::Full)
            ctx.r5.u32++;
    }

    __imp__sub_82619B88(ctx, base);
}
