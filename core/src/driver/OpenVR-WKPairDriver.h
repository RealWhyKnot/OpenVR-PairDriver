#pragma once

#ifdef OPENVRPAIRDRIVER_EXPORTS
#define OPENVRPAIRDRIVER_API extern "C" __declspec(dllexport)
#else
#define OPENVRPAIRDRIVER_API extern "C" __declspec(dllimport)
#endif
