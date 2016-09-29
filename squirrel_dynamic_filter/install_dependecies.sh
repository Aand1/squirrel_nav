 #!/bin/bash
 NUM_MAKE_THREADS=$(nproc)
 PROJECT_DIR="$(pwd)"
 sudo apt-get install libarmadillo-dev -y
 mkdir dependicies
 cd "$PROJECT_DIR"/dependicies
 git clone  https://github.com/mlpack/mlpack.git 
 cd mlpack
 mkdir build
 cd build 
 cmake ..
 make -j${NUM_MAKE_THREADS} 
 sudo make install
 sudo apt-get install libsuitesparse-dev -y
 sudo apt-get install libqglviewer-dev -y
 cd "$PROJECT_DIR"/dependicies
 git clone https://github.com/RainerKuemmerle/g2o.git 
 cd g2o
 mkdir build
 cd build
 cmake ..
 make -j${NUM_MAKE_THREADS}
 echo export G2O_ROOT="$PROJECT_DIR"/dependencies/g2o >> ~/.bashrc
 source ~/.bashrc
 cp "$PROJECT_DIR"/dependicies/g2o/build/g2o/config.h "$PROJECT_DIR"/dependicies/g2o/g2o/
 
