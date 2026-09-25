# VESC `mc_configuration` 字节流 — 机械对照表

> 上游：`vendor/bldc/confgenerator.c` 的 `confgenerator_serialize_mcconf()`。
> 本文是**从源码机械提取**的结果，不是手写转录；提取方式写在下面，可复现。
> 用途：`COMM_GET_MCCONF` / `COMM_SET_MCCONF` 与 flash 持久化共用的那份字节流
> （阶段 C2 / A7）。计划与判据见 [`bldc-migration.md`](bldc-migration.md)。

## 提取方式

1. 取 `confgenerator_serialize_mcconf()` 的函数体（从签名行到配对的收尾大括号）；
2. 按出现顺序解析每一条 `buffer_append_*` / `buffer[ind++]`，记录类型、字段名、缩放；
3. 累加每条语句的字节数得到偏移量。

## 头部事实（这些决定实现方式）

| 事实 | 值 |
|---|---|
| 字段数 | 203 |
| 序列化总长 | **484 字节**（固定，与内容无关；末字节偏移 484） |
| 首字段 | `uint32` `MCCONF_SIGNATURE` |
| 版本号 / 长度 / CRC | **都没有**（结尾只有 `return ind;`） |
| 字段类型 | `float32_auto` 69、`float16` 68、`uint8` 62、`int32` 1、`uint16` 1、`uint32` 2、其余为数组元素 |

## 由此得出的硬约束

字节流是**固定顺序布局**，字段之间没有分隔、没有标签。因此：

- **字段集不完整就等于格式不兼容**：少一个字段，其后每个字节都错位，上位机读到的是垃圾。
  所以 C1「只补有消费者的字段」这条规则**在这里不成立** —— 协议本身就是消费者，每个字段都被消费。
- 端口现有的 `app/motor_config/src/serialization.c` 是自造格式（signature + schema_ver +
  payload_len + CRC16 框），与本表无关；它需要被这份流取代，而不是被扩展。
- 端口现有 `MOTOR_CONFIG_BUFFER_SIZE = 256` 装不下 488 字节。

## 字段表

| 0 | `uint32` | `MCCONF_SIGNATURE` | - |
| 4 | `uint8` | `conf->pwm_mode` | - |
| 5 | `uint8` | `conf->comm_mode` | - |
| 6 | `uint8` | `conf->motor_type` | - |
| 7 | `uint8` | `conf->sensor_mode` | - |
| 8 | `float32_auto` | `conf->l_current_max` | - |
| 12 | `float32_auto` | `conf->l_current_min` | - |
| 16 | `float32_auto` | `conf->l_in_current_max` | - |
| 20 | `float32_auto` | `conf->l_in_current_min` | - |
| 24 | `float16` | `conf->l_in_current_map_start` | 10000 |
| 26 | `float16` | `conf->l_in_current_map_filter` | 10000 |
| 28 | `float32_auto` | `conf->l_abs_current_max` | - |
| 32 | `float32_auto` | `conf->l_min_erpm` | - |
| 36 | `float32_auto` | `conf->l_max_erpm` | - |
| 40 | `float16` | `conf->l_erpm_start` | 10000 |
| 42 | `float32_auto` | `conf->l_max_erpm_fbrake` | - |
| 46 | `float32_auto` | `conf->l_max_erpm_fbrake_cc` | - |
| 50 | `float16` | `conf->l_min_vin` | 10 |
| 52 | `float16` | `conf->l_max_vin` | 10 |
| 54 | `float16` | `conf->l_battery_cut_start` | 10 |
| 56 | `float16` | `conf->l_battery_cut_end` | 10 |
| 58 | `float16` | `conf->l_battery_regen_cut_start` | 10 |
| 60 | `float16` | `conf->l_battery_regen_cut_end` | 10 |
| 62 | `uint8` | `conf->l_slow_abs_current` | - |
| 63 | `uint8` | `conf->l_temp_fet_start` | - |
| 64 | `uint8` | `conf->l_temp_fet_end` | - |
| 65 | `uint8` | `conf->l_temp_motor_start` | - |
| 66 | `uint8` | `conf->l_temp_motor_end` | - |
| 67 | `float16` | `conf->l_temp_accel_dec` | 10000 |
| 69 | `float16` | `conf->l_min_duty` | 10000 |
| 71 | `float16` | `conf->l_max_duty` | 10000 |
| 73 | `float32_auto` | `conf->l_watt_max` | - |
| 77 | `float32_auto` | `conf->l_watt_min` | - |
| 81 | `float16` | `conf->l_current_max_scale` | 10000 |
| 83 | `float16` | `conf->l_current_min_scale` | 10000 |
| 85 | `float16` | `conf->l_duty_start` | 10000 |
| 87 | `uint8` | `conf->l_additional_faults` | - |
| 88 | `float32_auto` | `conf->sl_min_erpm` | - |
| 92 | `float32_auto` | `conf->sl_min_erpm_cycle_int_limit` | - |
| 96 | `float32_auto` | `conf->sl_max_fullbreak_current_dir_change` | - |
| 100 | `float16` | `conf->sl_cycle_int_limit` | 10 |
| 102 | `float16` | `conf->sl_phase_advance_at_br` | 10000 |
| 104 | `float32_auto` | `conf->sl_cycle_int_rpm_br` | - |
| 108 | `float32_auto` | `conf->sl_bemf_coupling_k` | - |
| 112 | `uint8` | `conf->hall_table[0]` | - |
| 113 | `uint8` | `conf->hall_table[1]` | - |
| 114 | `uint8` | `conf->hall_table[2]` | - |
| 115 | `uint8` | `conf->hall_table[3]` | - |
| 116 | `uint8` | `conf->hall_table[4]` | - |
| 117 | `uint8` | `conf->hall_table[5]` | - |
| 118 | `uint8` | `conf->hall_table[6]` | - |
| 119 | `uint8` | `conf->hall_table[7]` | - |
| 120 | `float32_auto` | `conf->hall_sl_erpm` | - |
| 124 | `float32_auto` | `conf->foc_current_kp` | - |
| 128 | `float32_auto` | `conf->foc_current_ki` | - |
| 132 | `float32_auto` | `conf->foc_f_zv` | - |
| 136 | `float32_auto` | `conf->foc_dt_us` | - |
| 140 | `uint8` | `conf->foc_encoder_inverted` | - |
| 141 | `float32_auto` | `conf->foc_encoder_offset` | - |
| 145 | `float32_auto` | `conf->foc_encoder_ratio` | - |
| 149 | `uint8` | `conf->foc_sensor_mode` | - |
| 150 | `float32_auto` | `conf->foc_pll_kp` | - |
| 154 | `float32_auto` | `conf->foc_pll_ki` | - |
| 158 | `float32_auto` | `conf->foc_motor_l` | - |
| 162 | `float32_auto` | `conf->foc_motor_ld_lq_diff` | - |
| 166 | `float32_auto` | `conf->foc_motor_r` | - |
| 170 | `float32_auto` | `conf->foc_motor_flux_linkage` | - |
| 174 | `float32_auto` | `conf->foc_observer_gain` | - |
| 178 | `float32_auto` | `conf->foc_observer_gain_slow` | - |
| 182 | `float16` | `conf->foc_observer_offset` | 1000 |
| 184 | `float32_auto` | `conf->foc_duty_dowmramp_kp` | - |
| 188 | `float32_auto` | `conf->foc_duty_dowmramp_ki` | - |
| 192 | `float16` | `conf->foc_start_curr_dec` | 10000 |
| 194 | `float32_auto` | `conf->foc_start_curr_dec_rpm` | - |
| 198 | `float32_auto` | `conf->foc_openloop_rpm` | - |
| 202 | `float16` | `conf->foc_openloop_rpm_low` | 1000 |
| 204 | `float16` | `conf->foc_sl_openloop_hyst` | 100 |
| 206 | `float16` | `conf->foc_sl_openloop_time_lock` | 100 |
| 208 | `float16` | `conf->foc_sl_openloop_time_ramp` | 100 |
| 210 | `float16` | `conf->foc_sl_openloop_time` | 100 |
| 212 | `float16` | `conf->foc_sl_openloop_boost_q` | 100 |
| 214 | `float16` | `conf->foc_sl_openloop_max_q` | 100 |
| 216 | `uint8` | `conf->foc_hall_table[0]` | - |
| 217 | `uint8` | `conf->foc_hall_table[1]` | - |
| 218 | `uint8` | `conf->foc_hall_table[2]` | - |
| 219 | `uint8` | `conf->foc_hall_table[3]` | - |
| 220 | `uint8` | `conf->foc_hall_table[4]` | - |
| 221 | `uint8` | `conf->foc_hall_table[5]` | - |
| 222 | `uint8` | `conf->foc_hall_table[6]` | - |
| 223 | `uint8` | `conf->foc_hall_table[7]` | - |
| 224 | `float32_auto` | `conf->foc_hall_interp_erpm` | - |
| 228 | `float32_auto` | `conf->foc_sl_erpm_start` | - |
| 232 | `float32_auto` | `conf->foc_sl_erpm` | - |
| 236 | `uint8` | `conf->foc_control_sample_mode` | - |
| 237 | `uint8` | `conf->foc_current_sample_mode` | - |
| 238 | `uint8` | `conf->foc_sat_comp_mode` | - |
| 239 | `float16` | `conf->foc_sat_comp` | 1000 |
| 241 | `uint8` | `conf->foc_temp_comp` | - |
| 242 | `float16` | `conf->foc_temp_comp_base_temp` | 100 |
| 244 | `float16` | `conf->foc_current_filter_const` | 10000 |
| 246 | `uint8` | `conf->foc_cc_decoupling` | - |
| 247 | `uint8` | `conf->foc_observer_type` | - |
| 248 | `uint8` | `conf->foc_hfi_amb_mode` | - |
| 249 | `float16` | `conf->foc_hfi_amb_current` | 10 |
| 251 | `uint8` | `conf->foc_hfi_amb_tres` | - |
| 252 | `float16` | `conf->foc_hfi_voltage_start` | 10 |
| 254 | `float16` | `conf->foc_hfi_voltage_run` | 10 |
| 256 | `float16` | `conf->foc_hfi_voltage_max` | 10 |
| 258 | `float16` | `conf->foc_hfi_gain` | 1000 |
| 260 | `float16` | `conf->foc_hfi_max_err` | 1000 |
| 262 | `float16` | `conf->foc_hfi_hyst` | 100 |
| 264 | `float32_auto` | `conf->foc_sl_erpm_hfi` | - |
| 268 | `float32_auto` | `conf->foc_hfi_reset_erpm` | - |
| 272 | `uint16` | `conf->foc_hfi_start_samples` | - |
| 274 | `float32_auto` | `conf->foc_hfi_obs_ovr_sec` | - |
| 278 | `uint8` | `conf->foc_hfi_samples` | - |
| 279 | `uint8` | `conf->foc_offsets_cal_mode` | - |
| 280 | `float32_auto` | `conf->foc_offsets_current[0]` | - |
| 284 | `float32_auto` | `conf->foc_offsets_current[1]` | - |
| 288 | `float32_auto` | `conf->foc_offsets_current[2]` | - |
| 292 | `float16` | `conf->foc_offsets_voltage[0]` | 10000 |
| 294 | `float16` | `conf->foc_offsets_voltage[1]` | 10000 |
| 296 | `float16` | `conf->foc_offsets_voltage[2]` | 10000 |
| 298 | `float16` | `conf->foc_offsets_voltage_undriven[0]` | 10000 |
| 300 | `float16` | `conf->foc_offsets_voltage_undriven[1]` | 10000 |
| 302 | `float16` | `conf->foc_offsets_voltage_undriven[2]` | 10000 |
| 304 | `uint8` | `conf->foc_phase_filter_enable` | - |
| 305 | `uint8` | `conf->foc_phase_filter_disable_fault` | - |
| 306 | `float32_auto` | `conf->foc_phase_filter_max_erpm` | - |
| 310 | `uint8` | `conf->foc_mtpa_mode` | - |
| 311 | `float32_auto` | `conf->foc_fw_current_max` | - |
| 315 | `float16` | `conf->foc_fw_duty_start` | 10000 |
| 317 | `float16` | `conf->foc_fw_ramp_time` | 1000 |
| 319 | `float16` | `conf->foc_fw_q_current_factor` | 10000 |
| 321 | `float16` | `conf->foc_fw_backoff` | 1000 |
| 323 | `uint8` | `conf->foc_speed_soure` | - |
| 324 | `uint8` | `conf->foc_short_ls_on_zero_duty` | - |
| 325 | `float16` | `conf->foc_overmod_factor` | 10000 |
| 327 | `float16` | `conf->foc_mag_vd_max` | 10000 |
| 329 | `uint8` | `conf->sp_pid_loop_rate` | - |
| 330 | `float32_auto` | `conf->s_pid_kp` | - |
| 334 | `float32_auto` | `conf->s_pid_ki` | - |
| 338 | `float32_auto` | `conf->s_pid_kd` | - |
| 342 | `float16` | `conf->s_pid_kd_filter` | 10000 |
| 344 | `float32_auto` | `conf->s_pid_min_erpm` | - |
| 348 | `uint8` | `conf->s_pid_allow_braking` | - |
| 349 | `float32_auto` | `conf->s_pid_ramp_erpms_s` | - |
| 353 | `uint8` | `conf->s_pid_speed_source` | - |
| 354 | `float32_auto` | `conf->p_pid_kp` | - |
| 358 | `float32_auto` | `conf->p_pid_ki` | - |
| 362 | `float32_auto` | `conf->p_pid_kd` | - |
| 366 | `float32_auto` | `conf->p_pid_kd_proc` | - |
| 370 | `float16` | `conf->p_pid_kd_filter` | 10000 |
| 372 | `float32_auto` | `conf->p_pid_ang_div` | - |
| 376 | `float16` | `conf->p_pid_gain_dec_angle` | 10 |
| 378 | `float32_auto` | `conf->p_pid_offset` | - |
| 382 | `float16` | `conf->cc_startup_boost_duty` | 10000 |
| 384 | `float32_auto` | `conf->cc_min_current` | - |
| 388 | `float32_auto` | `conf->cc_gain` | - |
| 392 | `float16` | `conf->cc_ramp_step_max` | 10000 |
| 394 | `??` | `buffer_append_int32(buffer, conf->m_fault_stop_time_ms, &ind` | - |
| 394 | `float16` | `conf->m_duty_ramp_step` | 10000 |
| 396 | `float32_auto` | `conf->m_current_backoff_gain` | - |
| 400 | `uint32` | `conf->m_encoder_counts` | - |
| 404 | `float16` | `conf->m_encoder_sin_amp` | 1000 |
| 406 | `float16` | `conf->m_encoder_cos_amp` | 1000 |
| 408 | `float16` | `conf->m_encoder_sin_offset` | 1000 |
| 410 | `float16` | `conf->m_encoder_cos_offset` | 1000 |
| 412 | `float16` | `conf->m_encoder_sincos_filter_constant` | 1000 |
| 414 | `float16` | `conf->m_encoder_sincos_phase_correction` | 1000 |
| 416 | `uint8` | `conf->m_sensor_port_mode` | - |
| 417 | `uint8` | `conf->m_invert_direction` | - |
| 418 | `uint8` | `conf->m_drv8301_oc_mode` | - |
| 419 | `uint8` | `conf->m_drv8301_oc_adj` | - |
| 420 | `float32_auto` | `conf->m_bldc_f_sw_min` | - |
| 424 | `float32_auto` | `conf->m_bldc_f_sw_max` | - |
| 428 | `float32_auto` | `conf->m_dc_f_sw` | - |
| 432 | `float32_auto` | `conf->m_ntc_motor_beta` | - |
| 436 | `uint8` | `conf->m_out_aux_mode` | - |
| 437 | `uint8` | `conf->m_motor_temp_sens_type` | - |
| 438 | `float32_auto` | `conf->m_ptc_motor_coeff` | - |
| 442 | `float16` | `conf->m_ntcx_ptcx_res` | 0.1 |
| 444 | `float16` | `conf->m_ntcx_ptcx_temp_base` | 10 |
| 446 | `uint8` | `conf->m_hall_extra_samples` | - |
| 447 | `uint8` | `conf->m_batt_filter_const` | - |
| 448 | `uint8` | `conf->si_motor_poles` | - |
| 449 | `float32_auto` | `conf->si_gear_ratio` | - |
| 453 | `float32_auto` | `conf->si_wheel_diameter` | - |
| 457 | `uint8` | `conf->si_battery_type` | - |
| 458 | `uint8` | `conf->si_battery_cells` | - |
| 459 | `float32_auto` | `conf->si_battery_ah` | - |
| 463 | `float32_auto` | `conf->si_motor_nl_current` | - |
| 467 | `uint8` | `conf->bms.type` | - |
| 468 | `uint8` | `conf->bms.limit_mode` | - |
| 469 | `uint8` | `conf->bms.t_limit_start` | - |
| 470 | `uint8` | `conf->bms.t_limit_end` | - |
| 471 | `float16` | `conf->bms.soc_limit_start` | 1000 |
| 473 | `float16` | `conf->bms.soc_limit_end` | 1000 |
| 475 | `float16` | `conf->bms.vmin_limit_start` | 1000 |
| 477 | `float16` | `conf->bms.vmin_limit_end` | 1000 |
| 479 | `float16` | `conf->bms.vmax_limit_start` | 1000 |
| 481 | `float16` | `conf->bms.vmax_limit_end` | 1000 |
| 483 | `uint8` | `conf->bms.fwd_can_mode` | - |

## 编码语义（实测，不是推断）

用原版 `util/buffer.c` 跑出来的结果：

| 值 | `float32_auto` | `float16`（scale 1e2） |
| --- | --- | --- |
| 0.5 | `3F 00 00 00` | `00 32` |
| 60.0 | `42 70 00 00` | `17 70` |
| -60.0 | `C2 70 00 00` | `E8 90` |
| 25000.0 | `46 C3 50 00` | `25 A0` |
| 1e-7 | `33 D6 BF 95` | `00 00` |
| 0.95 | `3F 73 33 33` | `00 5F` |
| -1.0 | `BF 80 00 00` | `FF 9C` |

结论：

- **`float32_auto` 就是 IEEE-754 大端 float32**（`frexpf` 那套只是把同一个位型重建一遍），
  唯一区别是**亚正规数先被置为 0**（`fabsf(x) < 1.5e-38`）。**它不是变长编码**，恒为 4 字节。
- **`float16` 就是 int16 大端**：`(int16_t)(value * scale)`，恒为 2 字节。
- 端口 `infra/vesc_buffer` 的 `vesc_buffer_append_float32_auto` 与
  `vesc_buffer_append_float16` 与上表**逐字节一致**（8/8 实测，同一条打印程序分别链接两边）。
  所以 C2 不需要新编码器：只需让 `motor_config` 改用它们，并按字段对齐类型与缩放。

（对照：端口现在的 `append_float` 是「int32 大端 × scale」的定点，原版只在一个字段上用这种
形式；拿它当默认编码是错的 —— 这正是 C2 要换掉的。）

## 权威参考向量

`confgenerator_set_defaults_mcconf()` 之后 `confgenerator_serialize_mcconf()` 的输出：
**488 字节** = 4 字节签名 `BC 09 F8 B0` + 484 字节字段区，端到端解码回环为真。
这就是 C2/A7 的对照目标（端口序列化器必须逐字节等于它）：

```text
0xBC,0x09,0xF8,0xB0,0x01,0x00,0x02,0x00,0x42,0x70,0x00,0x00,
0xC2,0x70,0x00,0x00,0x42,0xC6,0x00,0x00,0xC2,0x70,0x00,0x00,
0x23,0x28,0x00,0x14,0x43,0x02,0x00,0x00,0xC7,0xC3,0x50,0x00,
0x47,0xC3,0x50,0x00,0x1F,0x40,0x43,0x96,0x00,0x00,0x44,0xBB,
0x80,0x00,0x00,0x50,0x02,0x3A,0x00,0x64,0x00,0x50,0x27,0x10,
0x2A,0xF8,0x00,0x55,0x64,0x55,0x64,0x05,0xDC,0x00,0x32,0x25,
0x1C,0x49,0xB7,0x1B,0x00,0xC9,0xB7,0x1B,0x00,0x27,0x10,0x27,
0x10,0x27,0x10,0x00,0x43,0x16,0x00,0x00,0x44,0x89,0x80,0x00,
0x41,0x20,0x00,0x00,0x02,0x6C,0x1F,0x40,0x47,0x9C,0x40,0x00,
0x44,0x16,0x00,0x00,0xFF,0x01,0x03,0x02,0x05,0x06,0x04,0xFF,
0x44,0xFA,0x00,0x00,0x3C,0xF5,0xC2,0x8F,0x42,0x48,0x00,0x00,
0x46,0xC3,0x50,0x00,0x3D,0xF5,0xC2,0x8F,0x00,0x43,0x34,0x00,
0x00,0x40,0xE0,0x00,0x00,0x00,0x44,0xFA,0x00,0x00,0x46,0xEA,
0x60,0x00,0x36,0xEA,0xE1,0x8B,0x00,0x00,0x00,0x00,0x3C,0x75,
0xC2,0x8F,0x3B,0x20,0x90,0x2E,0x49,0x5B,0xBA,0x00,0x3D,0x4C,
0xCC,0xCD,0xFC,0x18,0x41,0xA0,0x00,0x00,0x43,0xC8,0x00,0x00,
0x27,0x10,0x45,0x1C,0x40,0x00,0x44,0xBB,0x80,0x00,0x00,0x00,
0x00,0x0A,0x00,0x00,0x00,0x0A,0x00,0x05,0x00,0x00,0xFF,0x9C,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x43,0xFA,0x00,0x00,
0x45,0x1C,0x40,0x00,0x45,0x5A,0xC0,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x09,0xC4,0x03,0xE8,0x00,0x03,0x00,0x02,0x58,0x0F,
0x00,0xC8,0x00,0x28,0x00,0x3C,0x01,0x2C,0x01,0x2C,0x00,0x00,
0x45,0x3B,0x80,0x00,0x43,0xFA,0x00,0x00,0x00,0x05,0x3A,0x83,
0x12,0x6F,0x01,0x01,0x45,0x00,0x00,0x00,0x45,0x00,0x00,0x00,
0x45,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x01,0x01,0x45,0x7A,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x1F,0x40,0x00,0x00,0x01,0xF4,0x07,0xD0,0x00,
0x00,0x27,0x10,0x26,0x48,0x05,0x3B,0x83,0x12,0x6F,0x3B,0x83,
0x12,0x6F,0x38,0xD1,0xB7,0x17,0x07,0xD0,0x44,0x61,0x00,0x00,
0x01,0x46,0xC3,0x50,0x00,0x00,0x3C,0xCC,0xCC,0xCD,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x39,0xB7,0x80,0x34,0x07,0xD0,
0x3F,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x64,
0x3D,0x4C,0xCC,0xCD,0x3B,0x96,0xBB,0x99,0x01,0x90,0x00,0x00,
0x01,0xF4,0x00,0xC8,0x3F,0x00,0x00,0x00,0x00,0x00,0x20,0x00,
0x03,0xE8,0x03,0xE8,0x06,0x72,0x06,0x72,0x01,0xF4,0x00,0x00,
0x00,0x00,0x00,0x10,0x45,0x3B,0x80,0x00,0x47,0x08,0xB8,0x00,
0x46,0xC3,0x50,0x00,0x45,0x53,0x40,0x00,0x00,0x00,0x3F,0x1C,
0x28,0xF6,0x03,0xE8,0x00,0xFA,0x03,0x2D,0x0E,0x40,0x40,0x00,
0x00,0x3D,0xA9,0xFB,0xE7,0x00,0x03,0x40,0xC0,0x00,0x00,0x3F,
0x80,0x00,0x00,0x01,0x03,0x2D,0x41,0x00,0x32,0x00,0x00,0x0B,
0x54,0x09,0xC4,0x10,0x68,0x10,0xCC,0x00,
```

## 再现方法（可复现）

1. 建临时目录，**把 `confgenerator.c` 与 `confgenerator.h` 复制进去** —— `#include
   "conf_general.h"` 会先找同目录，复制后 stub 才能生效；
2. stub `conf_general.h`：`#define HW_DEFAULT_ID 1`（真实值来自板级 hwconf，此处仅为让
   `set_defaults_appconf` 编译通过）并 `#include "mcconf_default.h"`、`"appconf_default.h"`；
3. stub `ch.h`：只需 `systime_t`/`msg_t` 两个 typedef（`datatypes.h` 只用到这里）；
4. 编译包含路径：`-I <stub> -I <临时目录> -I <repo根> -I <repo>/util -I <repo>/motor
   -I <repo>/applications`，链接 `util/buffer.c` + `confgenerator.c` + 打印程序。
   该文件还自带 `confgenerator_deserialize_mcconf()` 与 `set_defaults_mcconf()`，
   所以**解码器与默认值也都可直接用作对照**。
