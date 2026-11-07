// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <obs.h>
#include <algorithm>

namespace SentinelThumbnail {
// Draw the same OBS program image for a native display or a shared GPU target.
inline void drawProgram(uint32_t width, uint32_t height, const obs_video_info &video)
{
 const double scale=std::min(double(width)/video.base_width,double(height)/video.base_height);
 const int imageWidth=std::max(1,int(video.base_width*scale));
 const int imageHeight=std::max(1,int(video.base_height*scale));
 gs_viewport_push();
 gs_projection_push();
 gs_set_viewport((int(width)-imageWidth)/2,(int(height)-imageHeight)/2,imageWidth,imageHeight);
 gs_ortho(0,float(video.base_width),0,float(video.base_height),-100,100);
 obs_render_main_texture();
 gs_projection_pop();
 gs_viewport_pop();
}
}
