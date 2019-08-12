
#include <fstream>
#include <iomanip>
//#include <iostream>
#include <sstream>
//#include <vector>
#include <ctime>	// std::ctime()
#include <cstring>	// std::memset()
#include <cmath>	// std::exp()

#include <mutex>
#include <thread>

#include "misc.h"

using namespace std;

// --------------------
//  sync_out/sync_endl
// --------------------

std::ostream& operator<<(std::ostream& os, SyncCout sc) {

	static Mutex m;

	if (sc == IO_LOCK)
		m.lock();

	if (sc == IO_UNLOCK)
		m.unlock();

	return os;
}


// --- 以下、やねうら王で独自追加したコード

// --------------------
//  Timer
// --------------------

void sleep(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

