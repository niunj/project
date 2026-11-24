#pragma once
/// @file RayPayload.h
/// @brief Extended ray payload carrying atmospheric & cross-induction data.
///
/// Implements the `RayPayload` and `ScatterPoint` extensions described in
/// section 3.3 of the design document. These structures live in GPU SSBOs
/// shared between the Vulkan RT pipeline and the CUDA signal-processing
/// kernels.

#include <cstdint>
#include "vsgraysim/core/BandTypes.h"

namespace vsgraysim {

/// Layout-compatible scalar vector used for SSBO payloads.
struct Vec3 {
    float x, y, z;
};

/// Extended ray payload.  Lifetime: from ray emission through closest-hit
/// (and any continuation rays).  Final state is written into shared memory
/// at the end of the trace.
struct RayPayload {
    // --- Original generic fields ---------------------------------------
    Vec3   origin;
    Vec3   dir;
    float  distance;
    float  total_attenuation;
    float  scatter_power;
    uint32_t band_type;             ///< BandMask
    float  tx_power;                ///< Power carried by the ray (W)

    // --- Atmospheric propagation ---------------------------------------
    float  atm_total_atten;        ///< Accumulated atmospheric attenuation
    float  atm_scatter_power;      ///< Accumulated atmospheric scatter energy
    float  atm_refract_offset;     ///< Refraction induced angular offset (rad)
    int32_t atm_layer_id;          ///< Current atmosphere layer index

    // --- Cross-induction (crosstalk) -----------------------------------
    int32_t is_cross_induction;    ///< bool: 1 if this is a crosstalk ray
    uint32_t tx_sensor_id;          ///< Emitting sensor ID
    uint32_t rx_sensor_id;          ///< Receiving sensor ID (crosstalk sink)
    int32_t cross_type;             ///< CrossType
    float  cross_power;             ///< Crosstalk signal power (W)
    float  cross_delay;             ///< Crosstalk signal delay (s)
    float  cross_band_match;        ///< Band-overlap factor [0..1]
    uint32_t hit_geo_unit_id;       ///< Geometry unit ID hit (if any)
    uint32_t hit_object_id;         ///< Owning object ID hit (if any)

    // --- Electromagnetic wave physics (coherent radar) -----------------
    float  frequency;              ///< Carrier frequency (Hz)
    float  wavelength;             ///< Wavelength (m) = c/f
    float  phase_accum;            ///< Accumulated phase along path (rad)
    float  i_amplitude;            ///< In-phase amplitude (I)
    float  q_amplitude;            ///< Quadrature amplitude (Q)
    float  beam_gain_tx;           ///< Transmit beam pattern gain [0..1]
    float  beam_gain_rx;           ///< Receive beam pattern gain [0..1]
    float  reserved[1];            ///< Pad to multiple of 16
};

// 32 words × 4 bytes = 128 bytes (16-byte aligned)
static_assert(sizeof(RayPayload) % 16 == 0,
              "RayPayload must be 16-byte aligned for SSBO usage");

/// One scatter-point record produced by the RT pipeline and consumed by
/// the CUDA signal processors.
struct ScatterPoint {
    Vec3   position;
    Vec3   normal;
    float  amplitude;             ///< Scatter amplitude magnitude (linear)
    float  phase;                 ///< Total accumulated phase (rad)
    float  time_delay;            ///< Propagation delay (s)
    uint32_t band_type;           ///< BandMask
    uint32_t geo_unit_id;
    uint32_t object_id;
    uint32_t tx_sensor_id;
    uint32_t rx_sensor_id;

    // --- Atmospheric bookkeeping ---------------------------------------
    float  atm_atten;             ///< Atmospheric attenuation factor [0..1]
    float  atm_scatter;           ///< Atmospheric scatter energy (W)

    // --- Electromagnetic complex amplitude (I/Q) -----------------------
    float  i_amplitude;           ///< In-phase component
    float  q_amplitude;           ///< Quadrature component
    float  frequency;             ///< Carrier frequency (Hz)
    float  power;                 ///< Instantaneous power (W) = I^2 + Q^2

    // --- Crosstalk bookkeeping ----------------------------------------
    int32_t has_cross;            ///< bool
    float  cross_power;           ///< Crosstalk power (W)
    uint32_t cross_rx_sensor_id;  ///< Crosstalk sink sensor ID
    /// Transmit pulse index (0-based within the frame).  Populated by both
    /// GPU (via push constants) and CPU paths so signal formation can
    /// consume pulses chronologically.  Reuses the former reserved pad.
    uint32_t pulse_seq;
    /// Slow time of the transmit pulse within the frame (s, fire_time).
    /// Absolute slow time = frame-start sim_time + pulse_slow_time.
    float  pulse_slow_time;
    uint32_t reserved[3];           ///< Pad to multiple of 16
};

// 28 words × 4 bytes = 112 bytes (16-byte aligned)
static_assert(sizeof(ScatterPoint) % 16 == 0,
              "ScatterPoint must be 16-byte aligned for SSBO usage");

/// Crosstalk route descriptor consumed by the GPU cross-induction
/// detection kernel (minimal subset of Layer-2 RouteDescGPU).
struct CrossRouteGPU {
    uint32_t tx_sensor_id;
    uint32_t rx_sensor_id;
    int32_t  priority;
    float    coupling_factor;
    float    band_match;
};

/// Receiver aperture descriptor consumed by the GPU cross-induction
/// detection kernel (minimal subset of Layer-2 ApertureDescGPU).
struct ApertureGPU {
    uint32_t sensor_id;
    float    dir[3];        ///< Boresight direction (unit)
    float    half_angle;    ///< FoV half angle (rad)
    float    aperture_area; ///< Effective receive area (m^2)
};

/// Signal-formation descriptor consumed by the GPU framing kernels
/// (Layer 3b on device).  One launch forms the frame of a single sensor
/// directly from the device-resident scatter buffer, so the scatter data
/// never round-trips through the host.
struct FrameFormGPU {
    uint32_t mode;            ///< 0=VIS, 1=IR, 2=RADAR, 3=SAR
    uint32_t sensor_id;       ///< tx sensor whose scatter points form this frame
    uint32_t width;           ///< VIS/IR: image width,  RADAR/SAR: range gates
    uint32_t height;          ///< VIS/IR: image height, RADAR/SAR: doppler bins
    float    sensor_pos[3];   ///< aperture position (m)
    float    sensor_vel[3];   ///< platform velocity (m/s) for Doppler
    float    focal_length;    ///< VIS/IR optics (m)
    float    pixel_pitch;     ///< VIS/IR detector pitch (m)
    float    f_number;        ///< VIS/IR f-stop
    float    integration_time;///< VIS/IR detector integration (s)
    float    solar_irradiance;///< VIS: E_sun (W/m^2)
    float    netd;            ///< IR: noise-equivalent temperature diff (K)
    float    responsivity;    ///< IR: detector responsivity (V/W)
    float    carrier_hz;      ///< RADAR/SAR carrier (Hz)
    float    prf;             ///< RADAR/SAR pulse repetition frequency (Hz)
    float    fs;              ///< RADAR/SAR fast-time sampling rate (Hz)
    float    noise_floor;     ///< scatter amplitude cut-off (W)
    uint32_t coherent_pulses; ///< pulses accumulated into this frame (meta)
    uint32_t seed;            ///< per-frame noise seed
};

} // namespace vsgraysim
