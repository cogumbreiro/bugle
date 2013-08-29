#ifndef NO_ANNOTATIONS_H
#define NO_ANNOTATIONS_H

#ifndef ANNOTATIONS_H
#error no_annotations.h must be included after annotations.h
#endif

#undef    __non_temporal_loads_begin
#undef    __non_temporal_loads_end
#undef    __invariant
#undef    __global_invariant
#undef    __candidate_invariant
#undef    __candidate_global_invariant
#undef    __requires
#undef    __global_requires
#undef    __ensures
#undef    __global_ensures

#define __NOP ((void) 1)
#define  __non_temporal_loads_begin()     __NOP
#define  __non_temporal_loads_end()       __NOP
#define  __invariant(X)                   __NOP
#define  __global_invariant(X)            __NOP
#define  __candidate_invariant(X)         __NOP
#define  __candidate_global_invariant(X)  __NOP
#define  __requires(X)                    __NOP
#define  __global_requires(X)             __NOP
#define  __ensures(X)                     __NOP
#define  __global_ensures(X)              __NOP

#endif
