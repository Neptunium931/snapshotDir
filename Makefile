snapshotDir-static: snapshotDir.o
	$(CXX) -static $^ -o $@

snapshotDir: snapshotDir.mp.o
	$(CXX)  -fopenmp  $^ -o $@

%.o: %.cpp
	$(CXX) -std=c++20 -I./cereal/include/ -c -o $@ $<

%.mp.o: %.cpp
	$(CXX) -fopenmp -std=c++20 -I./cereal/include/ -c -o $@ $<
