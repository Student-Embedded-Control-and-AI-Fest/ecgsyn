#ifndef ECGSYN_MODEL_H
#define ECGSYN_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PI_D
#define PI_D 3.1415926535897932384626433832795
#endif

//#define USE_TUSTIN
#define USE_RK4

typedef struct {
    int   ecg_fs;
    int   internal_fs;
    int   n_beats;
    float noise_mv;
    float hr_mean;
    float hr_std;
    float flo;
    float fhi;
    float flo_std;
    float fhi_std;
    float lf_hf_ratio;
    int   seed_init;

    float xinitial;
    float yinitial;
    float zinitial;
} EcgSynParams;

typedef struct {
    float h;
    float fhi;
    long  rseed;

    float *rr;
    float *rrpc;
    int   rrpc_len;

    float ti[6];
    float ai[6];
    float bi[6];
} EcgSynContext;

void ecgsyn_init_default_params(EcgSynParams *p);
void ecgsyn_init_context(EcgSynContext *ctx);

bool build_block_mv(
    const EcgSynParams *p,
    float **out_mv,
    int *out_len,
    EcgSynContext *ctx,
    float *x_end,
    float *y_end,
    float *z_end);

void free_context(EcgSynContext *ctx);
bool implicit_tustin_step(float y[], float t0, float h, float yout[]);

#ifdef __cplusplus
}
#endif

#endif
