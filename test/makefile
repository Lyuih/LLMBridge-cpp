log: log.cc ../sdk/src/logger.cc
	g++ -o $@ $^ -std=c++17 -lspdlog -lfmt -lpthread

.PHONY:clean
clean:
	rm -rf log