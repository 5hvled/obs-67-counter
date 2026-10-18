#include "hand-detector.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <vector>

namespace {
struct Blob { int n=0; double sx=0, sy=0; };

static bool skin(uint8_t b,uint8_t g,uint8_t r)
{
    const int mx=std::max({(int)r,(int)g,(int)b});
    const int mn=std::min({(int)r,(int)g,(int)b});
    // Broad RGB skin gate; calibration UI will tighten this after first test.
    return r>70 && g>35 && b>20 && (mx-mn)>12 &&
           r>g && r>b && std::abs((int)r-(int)g)>8;
}
}

bool detect_hand_pair_bgra(const uint8_t *data,uint32_t w,uint32_t h,uint32_t stride,
                           float min_frac,HandPoint &a,HandPoint &b)
{
    a={}; b={};
    if(!data||w<32||h<32) return false;
    const int step=4;
    const int gw=(int)((w+step-1)/step), gh=(int)((h+step-1)/step);
    std::vector<uint8_t> mask((size_t)gw*gh), seen(mask.size());
    for(int y=0;y<gh;y++) for(int x=0;x<gw;x++){
        uint32_t px=std::min<uint32_t>((uint32_t)x*step,w-1);
        uint32_t py=std::min<uint32_t>((uint32_t)y*step,h-1);
        const uint8_t *p=data+(size_t)py*stride+(size_t)px*4;
        mask[(size_t)y*gw+x]=skin(p[0],p[1],p[2]);
    }
    std::vector<Blob> blobs;
    const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
    for(int y=0;y<gh;y++) for(int x=0;x<gw;x++){
        size_t id=(size_t)y*gw+x;
        if(!mask[id]||seen[id]) continue;
        Blob z; std::queue<std::pair<int,int>> q; q.push({x,y}); seen[id]=1;
        while(!q.empty()){
            auto [cx,cy]=q.front(); q.pop(); z.n++; z.sx+=cx; z.sy+=cy;
            for(int k=0;k<4;k++){int nx=cx+dx[k],ny=cy+dy[k];
                if(nx<0||ny<0||nx>=gw||ny>=gh) continue;
                size_t ni=(size_t)ny*gw+nx;
                if(mask[ni]&&!seen[ni]){seen[ni]=1;q.push({nx,ny});}
            }
        }
        if((float)z.n/(gw*gh)>=min_frac) blobs.push_back(z);
    }
    if(blobs.size()<2) return false;
    std::sort(blobs.begin(),blobs.end(),[](auto &x,auto &y){return x.n>y.n;});
    auto cv=[&](const Blob &z){HandPoint p; p.x=(float)(z.sx/z.n)/gw; p.y=(float)(z.sy/z.n)/gh;
        p.area=(float)z.n/(gw*gh); p.valid=true; return p;};
    a=cv(blobs[0]); b=cv(blobs[1]);
    // Keep identity deterministic by horizontal position. We only care about
    // vertical-order flips, so camera mirroring does not affect counting.
    if(a.x>b.x) std::swap(a,b);
    return true;
}
