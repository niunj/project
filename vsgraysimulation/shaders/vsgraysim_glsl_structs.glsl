// vsgraysim_glsl_structs.glsl
// GPU struct definitions mirroring the C++ structs in include/vsgraysim/core/.
// Included by all ray-tracing shaders.

struct Vec3 { float x, y, z; };

struct GeoUnitPhysParams {
    uint  object_id;
    uint  geo_unit_id;
    uint  band_mask;
    float vis_albedo;
    int   vis_brdf_type;
    float laser_scatter;
    float laser_absorb;
    float ir_emissivity[2];
    float ir_base_temp;
    float ir_thermal_conduct;
    uint  ir_temp_tex_id;
    uint  radar_rcs_lut_id;
    float radar_epsilon_real;
    float radar_epsilon_imag;
    float radar_surface_rough;
    float sar_sigma0_db;
    float sar_polar_couple;
    float sar_doppler_corr;
    float rcs_modify_factor;
    float temp_modify_offset;
    uint  padding;
};

struct BandParamMap {
    uint geo_unit_id;
    uint band_mask;
    uint param_offset;
    uint reserved;
};

struct AtmLayerParams {
    float height_min;
    float height_max;
    float temp;
    float pressure;
    float humidity;
    float density;
    float aerosol;
    float ext_coeff[4];
    float scatter_coeff[4];
    float absorb_coeff[4];
    float refract_index;
    float turbulence;
    float cloud_density;
    float reserved;
};

struct RayPayload {
    Vec3  origin;
    Vec3  dir;
    float distance;
    float total_attenuation;
    float scatter_power;
    uint  band_type;
    float tx_power;
    float atm_total_atten;
    float atm_scatter_power;
    float atm_refract_offset;
    int   atm_layer_id;
    int   is_cross_induction;
    uint  tx_sensor_id;
    uint  rx_sensor_id;
    int   cross_type;
    float cross_power;
    float cross_delay;
    float cross_band_match;
    uint  hit_geo_unit_id;
    uint  hit_object_id;
    float frequency;
    float wavelength;
    float phase_accum;
    float i_amplitude;
    float q_amplitude;
    float beam_gain_tx;
    float beam_gain_rx;
    float reserved;
};

struct ScatterPoint {
    Vec3  position;
    Vec3  normal;
    float amplitude;
    float phase;
    float time_delay;
    uint  band_type;
    uint  geo_unit_id;
    uint  object_id;
    uint  tx_sensor_id;
    uint  rx_sensor_id;
    float atm_atten;
    float atm_scatter;
    float i_amplitude;
    float q_amplitude;
    float frequency;
    float power;
    int   has_cross;
    float cross_power;
    uint  cross_rx_sensor_id;
    uint  pulse_seq;        // 帧内脉冲序号 (与 C++ ScatterPoint 同偏移)
    float pulse_slow_time;  // 帧内慢时间 (s, fire_time)
    uint  reserved0, reserved1, reserved2;
};
