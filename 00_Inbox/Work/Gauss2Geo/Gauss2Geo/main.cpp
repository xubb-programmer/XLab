#include <iostream>
#include <cmath>
#include <iomanip>

// CGCS2000 椭球参数
constexpr double A = 6378137.0;                     // 长半轴 (m)
constexpr double F = 1.0 / 298.257222101;           // 扁率
constexpr double E2 = 2 * F - F * F;                // 第一偏心率平方 e²
constexpr double E4 = E2 * E2;
constexpr double E6 = E4 * E2;

// 角度转弧度
inline constexpr double deg2rad(double deg) { return deg * M_PI / 180.0; }
inline constexpr double rad2deg(double rad) { return rad * 180.0 / M_PI; }

void gaussKrugerInverse(
    double X,               // Easting (含500km假东偏)
    double Y,               // Northing
    double L0_deg,          // 中央经线（度）
    double& lat_deg,        // 输出：纬度（度）
    double& lon_deg         // 输出：经度（度）
) {
    const double x = X - 500000.0;  // 去掉假东偏
    const double y = Y;

    // 椭球常数
    const double e2 = E2;
    const double ep2 = e2 / (1 - e2);  // 第二偏心率平方

    // 初始底点纬度 Bf（弧度）
    double Bf = y / A;

    // 迭代计算底点纬度 Bf，使其子午线弧长等于 y
    for (int i = 0; i < 5; ++i) {
        double sinBf = sin(Bf);
        // 子午线弧长 S(Bf)
        double S = A * (
            (1 - E2/4.0 - 3*E4/64.0 - 5*E6/256.0) * Bf
            - (3*E2/8.0 + 3*E4/32.0 + 45*E6/1024.0) * sin(2*Bf)
            + (15*E4/256.0 + 45*E6/1024.0) * sin(4*Bf)
            - (35*E6/3072.0) * sin(6*Bf)
        );

        // 子午圈曲率半径 Mf
        double Mf = A * (1 - e2) / pow(1 - e2 * sinBf * sinBf, 1.5);

        Bf += (y - S) / Mf;
    }

    // 计算最终纬度和经度
    double sinBf = sin(Bf);
    double cosBf = cos(Bf);
    double tanBf = tan(Bf);

    double Nf = A / sqrt(1 - e2 * sinBf * sinBf);
    double Mf = A * (1 - e2) / pow(1 - e2 * sinBf * sinBf, 1.5);

    double t2 = tanBf * tanBf;
    double eta2 = ep2 * cosBf * cosBf;

    // 纬度改正项 dB（弧度）
    double dB = (x * x * tanBf) / (2 * Mf * Nf)
                - (x * x * x * x * tanBf * (5 + 3*t2 + eta2 - 9*t2*eta2)) / (24 * Mf * pow(Nf, 3))
                + (x * x * x * x * x * x * tanBf * (61 + 90*t2 + 45*t2*t2)) / (720 * Mf * pow(Nf, 5));

    double B = Bf - dB;

    // 经度改正项 dL（弧度）
    double dL = x / (Nf * cosBf)
                - (x * x * x * (1 + 2*t2 + eta2)) / (6 * pow(Nf, 3) * cosBf)
                + (x * x * x * x * x * (5 + 28*t2 + 24*t2*t2 + 6*eta2 + 8*t2*eta2)) / (120 * pow(Nf, 5) * cosBf);

    double L = deg2rad(L0_deg) + dL;

    lat_deg = rad2deg(B);
    lon_deg = rad2deg(L);
}

int main(int argc, char *argv[])
{
    // 默认带号21，对应中央经线 21*6-3=123
    int zone = 21;
    double L0 = zone * 6.0 - 3.0;
    double lat, lon;

    // 两种合法参数格式：
    // 1. 2参数：X Y  使用默认带号21
    // 2. 3参数：X Y 带号  使用自定义带号
    if (argc != 3 && argc != 4) {
        std::cerr << "使用方法1(兼容旧版，默认6度带21)：./Gauss2CGCS2000 [高斯X] [高斯Y]" << std::endl;
        std::cerr << "使用方法2(自定义6度带号)：./Gauss2CGCS2000 [高斯X] [高斯Y] [带号]" << std::endl;
        std::cerr << "示例1：./Gauss2CGCS2000 454608.23 4500183.64" << std::endl;
        std::cerr << "示例2：./Gauss2CGCS2000 644544 4681677 19" << std::endl;
        std::cout << "0.000000 0.000000" << std::endl;
        return 1;
    }

    // 转换命令行参数为浮点型
    double gauss_x, gauss_y;
    try {
        gauss_x = std::stod(argv[1]);
        gauss_y = std::stod(argv[2]);
        // 传入3个参数时，读取第三个作为带号，重新计算L0
        if (argc == 4) {
            zone = std::stoi(argv[3]);
            L0 = zone * 6.0 - 3.0;
        }
    } catch (...) {
        std::cerr << "参数错误：请输入有效的数字（支持小数）" << std::endl;
        std::cout << "0.000000 0.000000" << std::endl;
        return 1;
    }

    gaussKrugerInverse(gauss_x, gauss_y, L0, lat, lon);
    std::cout << std::fixed << std::setprecision(8);
    std::cout << lat << " " << lon << std::endl;

    return 0;
}
