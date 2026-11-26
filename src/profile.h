#pragma once
#include <stddef.h>
#include <stdint.h>

struct Profile {
    char gl_vendor[128];
    char gl_renderer[128];
    char gl_version[192];
    char gl_ext_append[2048];

    char vk_device_name[256];
    uint32_t vk_vendor_id, vk_device_id, vk_driver_version;
    bool has_vk_vendor, has_vk_device, has_vk_driver;

    char b_model[64], b_brand[64], b_manufacturer[64], b_device[64], b_product[64];
    char b_board[64], b_hardware[64], b_soc_model[64], b_soc_manufacturer[64];
};

bool profile_parse(const char *text, Profile *out);
