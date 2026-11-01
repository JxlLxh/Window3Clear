#include <stdio.h>
#include <stdint.h>

void write32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
void write16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main() {
    FILE* f = fopen("icon.ico", "wb");
    if (!f) return 1;
    
    // ICO 文件头
    write16(f, 0); write16(f, 1); write16(f, 1);
    fputc(32, f); fputc(32, f); fputc(0, f); fputc(0, f);
    write16(f, 1); write16(f, 32);
    write32(f, 40 + 32*32*4 + 32*4); // 图像数据大小
    write32(f, 22); // 数据偏移
    
    // BITMAPINFOHEADER
    write32(f, 40); write32(f, 32); write32(f, 64);
    write16(f, 1); write16(f, 32); write32(f, 0);
    write32(f, 32*32*4); write32(f, 0); write32(f, 0); write32(f, 0); write32(f, 0);
    
    // 像素数据（BGRA 顺序）
    for(int y=31; y>=0; y--) {
        for(int x=0; x<32; x++) {
            float fx = x - 15.5f, fy = y - 15.5f;
            float afx = fx < 0 ? -fx : fx;
            float afy = fy < 0 ? -fy : fy;
            float rectDist = afx > afy ? afx : afy;
            uint32_t px = 0;
            if (rectDist <= 14.0f) {
                int r = 88, g = 166, b = 255; // 亮蓝色
                if (rectDist < 8.0f) { r = 255; g = 255; b = 255; } // 白色内圈
                if (rectDist < 5.0f) { r = 88; g = 166; b = 255; } // 镂空
                px = (255 << 24) | (r << 16) | (g << 8) | b;
            }
            fwrite(&px, 4, 1, f);
        }
    }
    
    // AND 掩码
    for(int i=0; i<32*4; i++) fputc(0, f);
    
    fclose(f);
    return 0;
}
