#pragma once

#include <cmath>
#include <vector>

// Small radix-2 FFT for real input (power-of-two sizes): a complex FFT of half
// the size plus a split step. Plain float arithmetic (std::complex multiplies
// go through slow NaN-safe library calls). Enough for analysis frames of
// 1024..8192 samples; tables are built once per instance.
class RealFft
{
public:
    explicit RealFft(int n) : m_n(n), m_half(n / 2)
    {
        int bits = 0;
        while ((1 << bits) < m_half)
            ++bits;
        m_bitrev.resize(size_t(m_half));
        for (int i = 0; i < m_half; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            m_bitrev[size_t(i)] = r;
        }
        m_cos.resize(size_t(m_half / 2));
        m_sin.resize(size_t(m_half / 2));
        for (int i = 0; i < m_half / 2; ++i) {
            m_cos[size_t(i)] = float(std::cos(2.0 * M_PI * i / m_half));
            m_sin[size_t(i)] = float(-std::sin(2.0 * M_PI * i / m_half));
        }
        m_scos.resize(size_t(m_half + 1));
        m_ssin.resize(size_t(m_half + 1));
        for (int k = 0; k <= m_half; ++k) {
            m_scos[size_t(k)] = float(std::cos(2.0 * M_PI * k / m_n));
            m_ssin[size_t(k)] = float(-std::sin(2.0 * M_PI * k / m_n));
        }
        m_re.resize(size_t(m_half));
        m_im.resize(size_t(m_half));
        m_window.resize(size_t(n));
        for (int i = 0; i < n; ++i)
            m_window[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * M_PI * i / n)); // Hann
    }

    int size() const { return m_n; }

    // in: n samples (the Hann window is applied here). out: |X[k]| for k = 0..n/2.
    void magnitudes(const float *in, std::vector<float> &out)
    {
        float *re = m_re.data(), *im = m_im.data();
        const float *w = m_window.data();
        for (int i = 0; i < m_half; ++i) {
            const int j = m_bitrev[size_t(i)];
            re[j] = in[2 * i] * w[2 * i];
            im[j] = in[2 * i + 1] * w[2 * i + 1];
        }
        for (int len = 2; len <= m_half; len <<= 1) {
            const int halfLen = len / 2, step = m_half / len;
            for (int i = 0; i < m_half; i += len) {
                for (int j = 0; j < halfLen; ++j) {
                    const float wr = m_cos[size_t(j * step)], wi = m_sin[size_t(j * step)];
                    const int a = i + j, b = a + halfLen;
                    const float vr = re[b] * wr - im[b] * wi;
                    const float vi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - vr;
                    im[b] = im[a] - vi;
                    re[a] += vr;
                    im[a] += vi;
                }
            }
        }
        // Split: X[k] = E[k] + W^k O[k], E = (Z[k] + conj Z[N/2-k]) / 2, O = -i (Z[k] - conj Z[N/2-k]) / 2
        out.resize(size_t(m_half + 1));
        for (int k = 0; k <= m_half; ++k) {
            const int a = k % m_half, b = (m_half - k) % m_half;
            const float zr = re[a], zi = im[a], cr = re[b], ci = -im[b];
            const float er = 0.5f * (zr + cr), ei = 0.5f * (zi + ci);
            // (z - c) * (-i/2) = ((zi - ci) / 2, -(zr - cr) / 2)
            const float orr = 0.5f * (zi - ci), oi = -0.5f * (zr - cr);
            const float tr = m_scos[size_t(k)], ti = m_ssin[size_t(k)];
            const float xr = er + (orr * tr - oi * ti);
            const float xi = ei + (orr * ti + oi * tr);
            out[size_t(k)] = std::sqrt(xr * xr + xi * xi);
        }
    }

private:
    int m_n, m_half;
    std::vector<int> m_bitrev;
    std::vector<float> m_cos, m_sin, m_scos, m_ssin, m_re, m_im, m_window;
};
