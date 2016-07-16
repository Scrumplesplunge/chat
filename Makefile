MAKEFLAGS = -j8
CXXFLAGS = -std=c++14 -Wall -Igen  \
					 -flto -O2 -s -ffunction-sections -fdata-sections -Wl,--gc-sections
LDFLAGS = -pthread -lscrump

.PHONY: all clean

all: bin/client bin/server

clean:
	rm -rf bin gen

bin:
	mkdir bin

gen:
	mkdir gen

bin/client: src/client.cc src/network.cc gen/message_type.cc | bin
	${CXX} $^ -o $@ ${CXXFLAGS} -lreadline ${LDFLAGS}

bin/server: src/server.cc src/network.cc gen/message_type.cc | bin
	${CXX} $^ -o $@ ${CXXFLAGS} -lscrumpmain ${LDFLAGS}

gen/message_type.h gen/message_type.cc: src/message_type.enum | gen bin/enum
	cd gen && ../bin/enum --input ../src/message_type.enum --name MessageType  \
		                    --output message_type

bin/enum: src/enum.cc | bin
	${CXX} $^ -o $@ ${CXXFLAGS} -lscrumpmain ${LDFLAGS}
