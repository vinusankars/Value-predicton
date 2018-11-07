#include <vector>
#include <deque>

// Coded by S. Vinu Sankar and S. Deepak Narayanan

struct MyFCMPredictor
{
  struct FirstLevelEntry 
  {
	uint64_t predictTimeIndex;
	uint64_t commitTimeIndex;
	uint64_t inflight;
	
	FirstLevelEntry() : predictTimeIndex(0), commitTimeIndex(0), inflight(0) {}
  };
  
  std::vector<FirstLevelEntry> firstLevelTable;
  
  struct SecondLevelEntry
  {
	uint64_t pred;
	uint8_t conf;
	
	SecondLevelEntry() : pred(0), conf(0) {};
  };
  
  std::vector<SecondLevelEntry> secondLevelTable;
  
  struct InflightInfo
  {
    uint64_t seqNum;
	uint64_t firstLevelIndex;
	uint64_t secondLevelIndex;
	
	InflightInfo() : seqNum(0), firstLevelIndex(0) {} 
	InflightInfo(uint64_t sn, uint64_t fidx, uint64_t sidx) : seqNum(sn), firstLevelIndex(fidx), secondLevelIndex(sidx) {}
  };

  struct Stride  // Structure for the stride
  {
	uint64_t pc; // stores the program counter value, 64 bits
	uint64_t stride; // stores the stride value, the step taken by PC, 64 bits
	uint64_t value; // the actual address value, 64 bits
  };

  std::vector<Stride> strideTable;
  
  std::deque<InflightInfo> inflightPreds;
  
  uint64_t globalHistoryRegister;
  uint64_t pathHistoryRegister;
  uint64_t addressHistoryRegister;
  
  uint64_t firstLevelMask;
  uint64_t lastPrediction;

  uint32_t strideCount;  // 32 bit count for stride, if it exceeds 10, clear the structure
  
public:
	
MyFCMPredictor() : globalHistoryRegister(0), pathHistoryRegister(0), addressHistoryRegister(0), strideCount(0)
{
	firstLevelTable.resize(4096 * 4);
	secondLevelTable.resize(4096 * 4);
	strideTable.resize(300); // Total 300*64*3 bits = 7.03125 KB
	firstLevelMask = (4096 * 4) - 1;
	lastPrediction = 0xdeadbeef;
}
};
