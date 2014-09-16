
#include "config.h"
#include "application.hpp"
#include <iostream>
//#include <mongo/client/gridfs.h>

using namespace gfsfcgi;

int main(int argc, char** argv) {
    try {
        return Factory(argc, argv).getApplication().run();
    } catch (std::exception& e) {
        std::cerr << "Uncaught exception: " << e.what() << std::endl;
    } catch(...) {
        std::cerr << "Unspecified application exception" << std::endl;
    }

	return 1;
}
