#include <vector>

int main(int argc, char *argv[])
{
    std::vector<double> a(512);
    std::vector<double> b(512);
    for(int i = 0; i < 512; ++i)
    {
        a[i] = i;
        b[i] = i;
    }
    std::vector<double> c(512);
    for(int i = 0; i < 512; ++i)
    {
        c[i] = a[i] + b[i];
    }
}