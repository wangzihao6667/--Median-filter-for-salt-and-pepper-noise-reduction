// CUDA kernels for salt-and-pepper noise and median filtering.
// This file is compiled at run time by NVRTC, so it must not include headers.
// All kernels use one z-slice per image and therefore process a whole batch in
// a single launch (grid.z = image count).

#define TILE 16

__device__ __forceinline__ unsigned int pcg_hash(unsigned int x) {
    unsigned int state = x * 747796405u + 2891336453u;
    unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

__device__ __forceinline__ float u01(unsigned int x) {
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

__device__ __forceinline__ unsigned char add_impulse(unsigned char v,
                                                      unsigned int seed,
                                                      int img, int x, int y,
                                                      int w, float p, int *hit) {
    unsigned int h = pcg_hash((unsigned int)((unsigned long long)y * (unsigned long long)w +
                                             (unsigned long long)x) ^
                              seed ^ ((unsigned int)img * 0x9e3779b9u));
    unsigned int h2 = pcg_hash(h ^ 0x85ebca6bu);
    *hit = (u01(h) < p) ? 1 : 0;
    if (*hit) {
        return (h2 & 1u) ? (unsigned char)255 : (unsigned char)0;
    }
    return v;
}

template <int N>
__device__ __forceinline__ unsigned char median_u8(unsigned char *v) {
    #pragma unroll
    for (int i = 1; i < N; ++i) {
        unsigned char key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) {
            v[j + 1] = v[j];
            --j;
        }
        v[j + 1] = key;
    }
    return v[N / 2];
}

// 19-comparator, 3x3 median network.  This is exact for nine inputs and is
// noticeably cheaper than a full insertion sort.
__device__ __forceinline__ unsigned char median3_network(unsigned char *p) {
    #define CMP(a, b) do { if (p[a] > p[b]) { unsigned char t = p[a]; p[a] = p[b]; p[b] = t; } } while (0)
    CMP(1, 2); CMP(4, 5); CMP(7, 8);
    CMP(0, 1); CMP(3, 4); CMP(6, 7);
    CMP(1, 2); CMP(4, 5); CMP(7, 8);
    CMP(0, 3); CMP(5, 8); CMP(4, 7);
    CMP(3, 6); CMP(1, 4); CMP(2, 5);
    CMP(4, 7); CMP(4, 2); CMP(6, 4);
    CMP(4, 2);
    #undef CMP
    return p[4];
}

template <int R>
__device__ __forceinline__ unsigned char median_channel(unsigned char *v) {
    if constexpr (R == 1) {
        return median3_network(v);
    } else {
        return median_u8<(2 * R + 1) * (2 * R + 1)>(v);
    }
}

extern "C" __global__ void salt_pepper_kernel(const unsigned char *__restrict__ src,
                                               unsigned char *__restrict__ noisy,
                                               int w, int h, float p,
                                               unsigned int seed) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int img = blockIdx.z;
    if (x >= w || y >= h) return;

    size_t off = ((size_t)img * (size_t)w * (size_t)h + (size_t)y * (size_t)w + (size_t)x) * 3u;
    int hit = 0;
    #pragma unroll
    for (int c = 0; c < 3; ++c) {
        noisy[off + c] = add_impulse(src[off + c], seed, img, x, y, w, p, &hit);
    }
}

template <int R>
__device__ __forceinline__ void median_global_impl(const unsigned char *__restrict__ src,
                                                   unsigned char *__restrict__ dst,
                                                   int w, int h) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int img = blockIdx.z;
    if (x >= w || y >= h) return;

    const unsigned char *in = src + (size_t)img * (size_t)w * (size_t)h * 3u;
    unsigned char *out = dst + (size_t)img * (size_t)w * (size_t)h * 3u;
    size_t off = ((size_t)y * (size_t)w + (size_t)x) * 3u;
    const int K = 2 * R + 1;

    #pragma unroll
    for (int c = 0; c < 3; ++c) {
        unsigned char v[K * K];
        int n = 0;
        #pragma unroll
        for (int dy = -R; dy <= R; ++dy) {
            int yy = y + dy;
            yy = yy < 0 ? 0 : (yy >= h ? h - 1 : yy);
            #pragma unroll
            for (int dx = -R; dx <= R; ++dx) {
                int xx = x + dx;
                xx = xx < 0 ? 0 : (xx >= w ? w - 1 : xx);
                v[n++] = in[((size_t)yy * (size_t)w + (size_t)xx) * 3u + c];
            }
        }
        out[off + c] = median_channel<R>(v);
    }
}

extern "C" __global__ void median3_kernel(const unsigned char *__restrict__ src,
                                           unsigned char *__restrict__ dst,
                                           int w, int h) {
    median_global_impl<1>(src, dst, w, h);
}

extern "C" __global__ void median5_kernel(const unsigned char *__restrict__ src,
                                           unsigned char *__restrict__ dst,
                                           int w, int h) {
    median_global_impl<2>(src, dst, w, h);
}

extern "C" __global__ void median7_kernel(const unsigned char *__restrict__ src,
                                           unsigned char *__restrict__ dst,
                                           int w, int h) {
    median_global_impl<3>(src, dst, w, h);
}

// Fuse noise generation and median filtering in one tiled kernel.
// Each block loads a (TILE + 2R) square, including a halo, into shared memory.
// The shared tile contains the noisy image, so the filter does not re-read the
// noisy image from global memory.  This removes one full-size global write and
// one full-size global read compared with the two-kernel version.
template <int R>
__device__ __forceinline__ void fused_impl(unsigned char *smem,
                                            const unsigned char *__restrict__ src,
                                            unsigned char *__restrict__ noisy,
                                            unsigned char *__restrict__ dst,
                                            int w, int h, float p,
                                            unsigned int seed, int write_noisy) {
    const int SW = TILE + 2 * R;
    const int K = 2 * R + 1;
    int tx = threadIdx.x;
    int ty = threadIdx.y;
    int bx = blockIdx.x * TILE;
    int by = blockIdx.y * TILE;
    int img = blockIdx.z;

    const unsigned char *in = src + (size_t)img * (size_t)w * (size_t)h * 3u;
    unsigned char *noisy_img = noisy + (size_t)img * (size_t)w * (size_t)h * 3u;
    unsigned char *out_img = dst + (size_t)img * (size_t)w * (size_t)h * 3u;

    for (int ly = ty; ly < SW; ly += TILE) {
        int gy = by + ly - R;
        gy = gy < 0 ? 0 : (gy >= h ? h - 1 : gy);
        for (int lx = tx; lx < SW; lx += TILE) {
            int gx = bx + lx - R;
            gx = gx < 0 ? 0 : (gx >= w ? w - 1 : gx);
            size_t gi = ((size_t)gy * (size_t)w + (size_t)gx) * 3u;
            size_t si = ((size_t)ly * (size_t)SW + (size_t)lx) * 3u;
            int hit = 0;
            #pragma unroll
            for (int c = 0; c < 3; ++c) {
                smem[si + c] = add_impulse(in[gi + c], seed, img, gx, gy, w, p, &hit);
            }
        }
    }
    __syncthreads();

    int x = bx + tx;
    int y = by + ty;
    if (x >= w || y >= h) return;

    size_t oi = ((size_t)y * (size_t)w + (size_t)x) * 3u;
    if (write_noisy) {
        size_t si = ((size_t)(ty + R) * (size_t)SW + (size_t)(tx + R)) * 3u;
        noisy_img[oi] = smem[si];
        noisy_img[oi + 1] = smem[si + 1];
        noisy_img[oi + 2] = smem[si + 2];
    }

    #pragma unroll
    for (int c = 0; c < 3; ++c) {
        unsigned char v[K * K];
        int n = 0;
        #pragma unroll
        for (int dy = -R; dy <= R; ++dy) {
            #pragma unroll
            for (int dx = -R; dx <= R; ++dx) {
                size_t si = ((size_t)(ty + R + dy) * (size_t)SW +
                             (size_t)(tx + R + dx)) * 3u + c;
                v[n++] = smem[si];
            }
        }
        out_img[oi + c] = median_channel<R>(v);
    }
}

extern "C" __global__ void fused3_kernel(const unsigned char *__restrict__ src,
                                          unsigned char *__restrict__ noisy,
                                          unsigned char *__restrict__ dst,
                                          int w, int h, float p,
                                          unsigned int seed, int write_noisy) {
    __shared__ unsigned char smem[(TILE + 2) * (TILE + 2) * 3];
    fused_impl<1>(smem, src, noisy, dst, w, h, p, seed, write_noisy);
}

extern "C" __global__ void fused5_kernel(const unsigned char *__restrict__ src,
                                          unsigned char *__restrict__ noisy,
                                          unsigned char *__restrict__ dst,
                                          int w, int h, float p,
                                          unsigned int seed, int write_noisy) {
    __shared__ unsigned char smem[(TILE + 4) * (TILE + 4) * 3];
    fused_impl<2>(smem, src, noisy, dst, w, h, p, seed, write_noisy);
}

extern "C" __global__ void fused7_kernel(const unsigned char *__restrict__ src,
                                          unsigned char *__restrict__ noisy,
                                          unsigned char *__restrict__ dst,
                                          int w, int h, float p,
                                          unsigned int seed, int write_noisy) {
    __shared__ unsigned char smem[(TILE + 6) * (TILE + 6) * 3];
    fused_impl<3>(smem, src, noisy, dst, w, h, p, seed, write_noisy);
}

// One block per image.  The block computes MSE against the clean source for
// both the noisy and filtered images, plus the actual number of impulses.
extern "C" __global__ void stats_kernel(const unsigned char *__restrict__ orig,
                                         const unsigned char *__restrict__ noisy,
                                         const unsigned char *__restrict__ filtered,
                                         double *__restrict__ mse_noisy,
                                         double *__restrict__ mse_filtered,
                                         unsigned long long *__restrict__ impulses,
                                         int w, int h) {
    int img = blockIdx.x;
    int t = threadIdx.x;
    size_t n = (size_t)w * (size_t)h * 3u;
    size_t base = (size_t)img * n;

    double sn = 0.0;
    double sf = 0.0;
    for (size_t i = t; i < n; i += blockDim.x) {
        int a = (int)orig[base + i];
        int bn = (int)noisy[base + i];
        int bf = (int)filtered[base + i];
        double dn = (double)(bn - a);
        double df = (double)(bf - a);
        sn += dn * dn;
        sf += df * df;
    }

    unsigned long long cnt = 0;
    size_t pixels = (size_t)w * (size_t)h;
    for (size_t p = t; p < pixels; p += blockDim.x) {
        size_t j = base + p * 3u;
        unsigned char v = noisy[j];
        if ((v == 0 || v == 255) && noisy[j + 1] == v && noisy[j + 2] == v &&
            (orig[j] != v || orig[j + 1] != v || orig[j + 2] != v)) {
            ++cnt;
        }
    }

    __shared__ double shn[256];
    __shared__ double shf[256];
    __shared__ unsigned long long shc[256];
    shn[t] = sn;
    shf[t] = sf;
    shc[t] = cnt;
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (t < s) {
            shn[t] += shn[t + s];
            shf[t] += shf[t + s];
            shc[t] += shc[t + s];
        }
        __syncthreads();
    }
    if (t == 0) {
        mse_noisy[img] = shn[0] / (double)n;
        mse_filtered[img] = shf[0] / (double)n;
        impulses[img] = shc[0];
    }
}
