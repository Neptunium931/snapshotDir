snapshotDir: snapshotDir.o 
	$(CXX)  $^ -o $@

%.o: %.cpp
	$(CXX) -std=c++20 -I./cereal/include/ -c -o $@ $<
