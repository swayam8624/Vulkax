#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc,char**argv){if(argc!=2){std::cerr<<"lab test requires vulkax-lab executable path\n";return 2;}auto output=std::filesystem::temp_directory_path()/"vulkax_lab_smoke";std::filesystem::remove_all(output);std::string command='"'+std::string(argv[1])+"\" suite --output \""+output.string()+"\" --particles 180 --steps 30 --dt 0.0002";int rc=std::system(command.c_str());if(rc!=0){std::cerr<<"vulkax-lab suite failed\n";return 1;}for(const auto&file:{output/"mill"/"velocity.ppm",output/"mill"/"particles.csv",output/"cfd"/"advected_scalar.ppm",output/"cfd"/"pressure.ppm",output/"material"/"model_fits.csv",output/"geometry"/"gyroid.obj"}){if(!std::filesystem::exists(file)||std::filesystem::file_size(file)<32){std::cerr<<"missing/empty lab output: "<<file<<'\n';return 1;}}std::filesystem::remove_all(output);std::cout<<"Vulkax lab end-to-end smoke passed\n";return 0;}
