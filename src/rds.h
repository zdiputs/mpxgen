/*
    PiFmAdv - Advanced FM transmitter for the Raspberry Pi
    Copyright (C) 2017 Miegl

    See https://github.com/Miegl/PiFmAdv
*/

#ifndef RDS_H
#define RDS_H


#include <stdint.h>

extern void get_rds_samples(float *buffer, int count);
extern void set_rds_pi(uint16_t pi_code);
extern void set_rds_rt(char *rt);
extern void set_rds_rt_dynamic(char *rt);
extern void set_rds_ps(char *ps);
extern void set_rds_ps_dynamic(char *ps);
extern void set_rds_rtp_flags(int rt_p_toggle, int rt_p_running);
extern void set_rds_rtp_tags(int rt_p_type_1, int rt_p_start_1, int rt_p_len_1,
                             int rt_p_type_2, int rt_p_start_2, int rt_p_len_2);
extern void set_rds_ta(int ta);
extern void set_rds_pty(int pty);
extern void set_rds_ptyn_enable(int enable_ptyn);
extern void set_rds_ptyn(char *ptyn);
extern void set_rds_ptyn_dynamic(char *ptyn);
extern void set_rds_af(int *af_array);
extern void set_rds_tp(int tp);
extern void set_rds_ms(int ms);
extern void set_rds_ab(int ab);


#endif /* RDS_H */
