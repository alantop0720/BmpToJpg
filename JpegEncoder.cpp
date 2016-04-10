#include "JpegEncoder.h"
#include <string.h>
#include <iostream>

using std::cout;
using std::vector;
using std::string;

namespace jpeg
{
    int zigzag_table[64] = {0, 1, 5, 6, 14, 15, 27, 28, 
                            2, 4, 7, 13, 16, 26, 29, 42, 
                            3, 8, 12, 17, 25, 30, 41, 43, 
                            9, 11, 18, 24, 31, 40, 44, 53, 
                            10, 19, 23, 32, 39, 45, 52, 54,
                            20, 22, 33, 38, 46, 51, 55, 60, 
                            21, 34, 37, 47, 50, 56, 59, 61, 
                            35, 36, 48, 49, 57, 58, 62, 63};
    double C(int u)
    {
        if (u == 0)
            return (1.0 / sqrt(2.0));
        else
            return 1.0;
    }

    void dct(double **block) 
    {
        double a;
        double F[8][8];
        for (int u = 0; u < 8; u++)
        for (int v = 0; v < 8; v++)
        {
            a = 0.0;
            for (int x = 0; x < 8; x++)
            for (int y = 0; y < 8; y++)
                a += block[x][y] * cos((2.0*x + 1.0)*u*3.14 / 16.0) * cos((2.0*y + 1.0)*v*3.14 / 16.0);
            F[u][v] = 0.25 * C(u) * C(v) * a;
        }

        for (int u = 0; u < 8; u++)
        for (int v = 0; v < 8; v++)
            block[u][v] = F[u][v];
    }

    void quantize(double **block, BYTE quan[64]) 
    {
        int x, y;
        for (int i = 0; i < 64; i++) {
            x = i / 8;
            y = i % 8;
            block[x][y] = block[x][y] / quan[i];
            if (block[x][y] > 65535 || block[x][y] < -65536) 
                block[x][y] > 0 ? block[x][y] = 65535 : block[x][y] = -65536;
        }
    }

    int* zigzagTransform(double **block)
    {
        int* res = new int[64];
        for (int i = 0; i < 8; i++)
        {
            for (int j = 0; j < 8; j++)
            {
                res[zigzag_table[i * 8 + j]] = int(block[i][j]);
            }
        }

        return res;
    }
    
    void  JpegEncoder::subsample()
    {
        int i, j, ii, jj;

        // chroma subsampling decide MCU
        if (chroma_subsampling == "4:4:4")
        {
            mcu_hor_count = img_width / 8; if (img_width % 8 != 0) mcu_hor_count++;
            mcu_ver_count = img_height / 8; if (img_height % 8 != 0) mcu_ver_count++;
            mcu_width = mcu_height = 8;
            y_hor_count = mcu_hor_count;
            y_ver_count = mcu_ver_count;
            y_block_count = y_hor_count * y_ver_count;
            c_hor_count = y_hor_count;
            c_ver_count = y_ver_count;
            c_block_count = y_block_count;
        }
        else if (chroma_subsampling == "4:2:2")
        {
            mcu_hor_count = img_width / 16; if (img_width % 16 != 0) mcu_hor_count++;
            mcu_ver_count = img_height / 8; if (img_height % 8 != 0) mcu_ver_count++;
            mcu_width = 16;
            mcu_height = 8;
            y_hor_count = mcu_hor_count * 2;
            y_ver_count = mcu_ver_count;
            y_block_count = y_hor_count * y_ver_count;
            c_hor_count = mcu_hor_count;
            c_ver_count = mcu_ver_count;
            c_block_count = c_hor_count * c_ver_count;
        }
        else // default 4:2:0
        {
            mcu_hor_count = img_width / 16; if (img_width % 16 != 0) mcu_hor_count++;
            mcu_ver_count = img_height / 16; if (img_height % 16 != 0) mcu_ver_count++;
            mcu_width = mcu_height = 16;
            y_hor_count = mcu_hor_count * 2;
            y_ver_count = mcu_ver_count * 2;
            y_block_count = y_hor_count * y_ver_count;
            c_hor_count = mcu_hor_count;
            c_ver_count = mcu_ver_count;
            c_block_count = c_hor_count * c_ver_count;
        }

        // init all blocks
        y_block = new double**[y_block_count];
        for (i = 0; i < y_block_count; i++)
        {
            y_block[i] = new double *[8];
            for (j = 0; j < 8; j++)
                y_block[i][j] = new double[8];
        }

        cr_block = new double **[c_block_count];
        cb_block = new double **[c_block_count];
        for (i = 0; i < c_block_count; i++)
        {
            cr_block[i] = new double *[8];
            cb_block[i] = new double *[8];
            for (j = 0; j < 8; j++)
            {
                cr_block[i][j] = new double[8];
                cb_block[i][j] = new double[8];
            }
        }

        // subsample y blocks
        int n = 0;
        for (i = 0; i < y_ver_count; i++)
        for (j = 0; j < y_hor_count; j++) 
        {
            for (ii = 0; ii < 8; ii++)
            for (jj = 0; jj < 8; jj++)
                if (i * 8 + ii < img_height && j * 8 + jj < img_width)
                {
                    Pixel ycc = rgb2ycc(origin[i * 8 + ii][j * 8 + jj]);
                    y_block[n][ii][jj] = ycc.v1;
                }
                else
                    y_block[n][ii][jj] = 0;
                    // this part is unstable
/*                    if (i * 8 + ii >= img_height)
                        y_block[n][ii][jj] = rgb2ycc(origin[img_height - 1][j * 8 + jj]).v1;
                    else if (j * 8 + jj >= img_width)
                        y_block[n][ii][jj] = rgb2ycc(origin[i * 8 + ii][img_width - 1]).v1;
                    else
                        y_block[n][ii][jj] = rgb2ycc(origin[img_height - 1][img_width - 1]).v1;
 */           n++;
        }

        // subsample cr&cb blocks
        n = 0;
        for (i = 0; i < c_ver_count; i++)
        for (j = 0; j < c_hor_count; j++) 
        {
            for (ii = 0; ii < 8; ii++)
            for (jj = 0; jj < 8; jj++)
            { 
                int img_i = i * mcu_height + ii * (mcu_height / 8);
                int img_j = j * mcu_width + jj * (mcu_width / 8);
                if (img_i < img_height && img_j < img_width)
                {
                    Pixel sum = origin[img_i][img_j];
                    int count = 0;
                    for (int x = 0; x < (mcu_height / 8); x++)
                        for (int y = 0; y < (mcu_width / 8); y++)
                            if (img_i + x < img_height && img_j + y < img_width)
                            {
                                count++;
                                sum = sum + origin[img_i + x][img_j + y];
                            }
                    sum = sum / (double)count;
                    Pixel ycc = rgb2ycc(sum);
                    cb_block[n][ii][jj] = ycc.v2;
                    cr_block[n][ii][jj] = ycc.v3;
                }
                else
                {
                    cb_block[n][ii][jj] = 0;
                    cr_block[n][ii][jj] = 0;
                    // this part is unstable
/*                    if (i * 8 + ii >= img_height)
                        cb_block[n][ii][jj] = rgb2ycc(origin[img_height - 1][j * 8 + jj]).v2;
                    else if (j * 8 + jj >= img_width)
                        cb_block[n][ii][jj] = rgb2ycc(origin[i * 8 + ii][img_width - 1]).v2;
                    else
                        cb_block[n][ii][jj] = rgb2ycc(origin[img_height - 1][img_width - 1]).v2;

                    if (i * 8 + ii >= img_height)
                        cr_block[n][ii][jj] = rgb2ycc(origin[img_height - 1][j * 8 + jj]).v3;
                    else if (j * 8 + jj >= img_width)
                        cr_block[n][ii][jj] = rgb2ycc(origin[i * 8 + ii][img_width - 1]).v3;
                    else
                        cr_block[n][ii][jj] = rgb2ycc(origin[img_height - 1][img_width - 1]).v3;
 */               }
            }
            n++;
        }
    }

    void JpegEncoder::dctAndQuan()
    {
        int i;
        BYTE *lum_quan, *croma_quan;
        switch (quality)
        {
        case 0:
            lum_quan = lum_quant0;
            croma_quan = croma_quant0;
        case 1:
            lum_quan = lum_quant1;
            croma_quan = croma_quant1;
            break;
        case 2:
            lum_quan = lum_quant2;
            croma_quan = croma_quant2;
            break;
        default:
            return;
        }

        for (i = 0; i < y_block_count; i++)
        {
            dct(y_block[i]);
            quantize(y_block[i], lum_quan);
        }
        for (i = 0; i < c_block_count; i++)
        {
            dct(cr_block[i]);
            quantize(cr_block[i], croma_quan);
            dct(cb_block[i]);
            quantize(cb_block[i], croma_quan);
        }
    }

    void JpegEncoder::zigzag()
    {
        int i;
        y_zigzag = new int*[y_block_count];
        for (i = 0; i < y_block_count; i++)
            y_zigzag[i] = zigzagTransform(y_block[i]);

        cr_zigzag = new int *[c_block_count];
        cb_zigzag = new int *[c_block_count];
        for (i = 0; i < c_block_count; i++)
        {
            cr_zigzag[i] = zigzagTransform(cr_block[i]);
            cb_zigzag[i] = zigzagTransform(cb_block[i]);
        }
        
    }

    void JpegEncoder::deltaEncoding()
    {
        int y_previous = y_zigzag[0][0];
        for (int i = 1; i < y_block_count; i++)
        {
            int y_current = y_zigzag[i][0];
            int y_delta = y_current - y_previous;
            y_previous = y_zigzag[i][0];
            y_zigzag[i][0] = y_delta;
        }

        int cr_previous = cr_zigzag[0][0];
        int cb_previous = cb_zigzag[0][0];
        for (int i = 1; i < c_block_count; i++)
        {
            int cr_current = cr_zigzag[i][0];
            int cr_delta = cr_current - cr_previous;
            cr_previous = cr_zigzag[i][0];
            cr_zigzag[i][0] = cr_delta;

            int cb_current = cb_zigzag[i][0];
            int cb_delta = cb_current - cb_previous;
            cb_previous = cb_zigzag[i][0];
            cb_zigzag[i][0] = cb_delta;
        }
    }

    void JpegEncoder::RLEAddPair(int zero_count, int n, vector<vector<int>> &ac)
    {
        vector<int> pair;
        pair.push_back(zero_count);
        pair.push_back(n);
        ac.push_back(pair);
    }

    void JpegEncoder::RLE(int **zigzag, int block_count, vector<vector<int>> &ac)
    {
        for (int i = 0; i < block_count; i++)
        {
            int zero_count = 0;
            int zero_pair = 0;
            for (int j = 1; j < 64; j++)
            {
                if (zigzag[i][j] == 0)
                {
                    if (j == 63)
                    {
                        RLEAddPair(0, 0, ac);
                        continue;
                    }
                    zero_count++;
                    if (zero_count == 15)
                    {
                        zero_pair++;
                        zero_count = 0;
                    }
                }
                else
                {
                    if (zero_pair != 0)
                    {
                        for (int k = 0; k < zero_pair; k++)
                            RLEAddPair(15, 0, ac);
                        zero_pair = 0;
                    }
                    RLEAddPair(zero_count, zigzag[i][j], ac);
                    zero_count = 0;
                }
            }
        }
    }

    void JpegEncoder::encodeImage(Pixel **matrix, int height, int width)
    {
        origin = matrix;
        img_height = height;
        img_width = width;

        subsample();
        dctAndQuan();
        zigzag();
        deltaEncoding();
        RLE(y_zigzag, y_block_count, y_ac);
        RLE(cr_zigzag, c_block_count, cr_ac);
        RLE(cb_zigzag, c_block_count, cb_ac);
    }

    JpegEncoder::JpegEncoder(int quality, string subsampling) :
        chroma_subsampling(subsampling),
        quality(quality)
    {

    }

    JpegEncoder::JpegEncoder()
    {
        quality = 1;
        chroma_subsampling = "4:2:0";
    }
}