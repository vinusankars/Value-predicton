#include <cinttypes>
#include "cvp.h"
#include "mypredictor.h"
#include <cassert>
#include <cstdio>
#include <stdlib.h>

// Coded by S. Vinu Sankar and S. Deepak Narayanan

static MyFCMPredictor predictor;

// Global branch and path history
static uint64_t ghr = 0, phist = 0;

// Load/Store address history
static uint64_t addrHist = 0;

bool getPrediction(uint64_t seq_no, uint64_t pc, uint8_t piece, uint64_t& predicted_value) {

  // Hash function generator for getting index to access the first level table
  uint64_t firstLevelIndex = (pc<<sizeof(ghr) + ghr) % predictor.firstLevelMask;
  // Accessing the second level table. Note that both tables are the same size so we are using the same mask.
  uint64_t secondLevelIndex = predictor.firstLevelTable[firstLevelIndex].predictTimeIndex % predictor.firstLevelMask;
// the bit-wise AND ensures that the index value would not go out of range
// Using the second level index we access the second level table to access the prediction value

  predictor.lastPrediction = predicted_value = predictor.secondLevelTable[secondLevelIndex].pred;


  if(!predictor.strideTable.empty() && abs(pc - predictor.strideTable.back().pc) == predictor.strideTable.back().stride)
// If a pattern in the stride is observed, then the confidence is returned to be 15 
// so that it will be speculated
  {
    predictor.secondLevelTable[secondLevelIndex].conf = 15;
  }

  uint8_t confidence = predictor.secondLevelTable[secondLevelIndex].conf;

  // Speculate using the prediction only if confidence is high enough
  return confidence >= 15;
}

void speculativeUpdate(uint64_t seq_no,    		// dynamic micro-instruction # (starts at 0 and increments indefinitely)
                       bool eligible,			// true: instruction is eligible for value prediction. false: not eligible.
		       uint8_t prediction_result,	// 0: incorrect, 1: correct, 2: unknown (not revealed)
		       // Note: can assemble local and global branch history using pc, next_pc, and insn.
		       uint64_t pc,			
		       uint64_t next_pc,
		       InstClass insn,
		       uint8_t piece,
		       // Note: up to 3 logical source register specifiers, up to 1 logical destination register specifier.
		       // 0xdeadbeef means that logical register does not exist.
		       // May use this information to reconstruct architectural register file state (using log. reg. and value at updatePredictor()).
		       uint64_t src1,
		       uint64_t src2,
		       uint64_t src3,
		       uint64_t dst) {

  //printf("%" PRIu64 "\n", src1);

  // In this example, we will attempt to predict ALU/LOAD/SLOWALU/undefInstClass/fpInstClass
  bool isPredictable = insn == aluInstClass || insn == loadInstClass || insn == slowAluInstClass || insn == fpInstClass || insn == undefInstClass;
 
  // Hash function generator, same as in the getPredction()
  uint64_t firstLevelIndex = (pc<<sizeof(ghr) + ghr) % predictor.firstLevelMask;
  uint64_t secondLevelIndex = predictor.firstLevelTable[firstLevelIndex].predictTimeIndex & predictor.firstLevelMask;
	
  // It's an instruction we are interested in predicting, update the first table history
  // Note that some other type of predictors may not want to update at this time if the
  // prediction is unknown to be correct or incorrect
  if(isPredictable)
  {
	predictor.inflightPreds.push_front({seq_no, firstLevelIndex, secondLevelIndex});
	predictor.firstLevelTable[firstLevelIndex].inflight++;
    // for FCM based prediction
    uint64_t result = predictor.lastPrediction; 
    predictor.firstLevelTable[firstLevelIndex].predictTimeIndex |= result;

    // for stride-based prediction
    // if stride pattern found, stride-based prediction is preferred over FCM based prediction
    if(!predictor.strideTable.empty() && abs(pc - predictor.strideTable.back().pc) == predictor.strideTable.back().stride && pc>predictor.strideTable.back().pc)
    {
      //result is predicted to be stride plus the current value
      result = predictor.strideTable.back().value + abs(predictor.strideTable.at(predictor.strideTable.size()-2).value-predictor.strideTable.back().value);
      predictor.firstLevelTable[firstLevelIndex].predictTimeIndex |= result;
    }

    //if stride pattern found and can be looked up in the structure
    if(!predictor.strideTable.empty() && predictor.strideTable.back().stride%abs(pc - predictor.strideTable.back().pc)==0 && pc<predictor.strideTable.back().pc)
    {
     // result is predicted according to the stride, and we index into the structure
     uint64_t diff = abs(pc - predictor.strideTable.back().pc)&&predictor.strideTable.back().stride;
      result = predictor.strideTable.at(predictor.strideTable.size()-1-diff).value;
      predictor.firstLevelTable[firstLevelIndex].predictTimeIndex = result;
    }
	return;
  }
  
  // At this point, any branch-related information is architectural, i.e.,
  // updating the GHR/LHRs is safe.
  bool isCondBr = insn == condBranchInstClass;
  bool isIndBr = insn == uncondIndirectBranchInstClass || uncondDirectBranchInstClass;

  // Infrastructure provides perfect branch prediction.
  // As a result, the branch-related histories can be updated now.
  if(isCondBr)
    ghr = (ghr << 2) | (pc != next_pc + 4);

  if(isIndBr)
	phist = (phist << 2) | (next_pc & 0x3);
}

void updatePredictor(uint64_t seq_no,		// dynamic micro-instruction #
		     uint64_t actual_addr,	// load or store address (0xdeadbeef if not a load or store instruction)
		     uint64_t actual_value,	// value of destination register (0xdeadbeef if instr. is not eligible for value prediction)
		     uint64_t actual_latency) {	// actual execution latency of instruction

  std::deque<MyFCMPredictor::InflightInfo> & inflight = predictor.inflightPreds;

  if(!predictor.strideTable.empty())
  {
    uint64_t last_pc = predictor.strideTable.back().pc;
    uint64_t stride = predictor.strideTable.back().stride;

  if(actual_addr - last_pc == stride || stride == 0)
  {
   predictor.strideTable.push_back({actual_addr, stride, actual_value});
  }

  else
  {
    predictor.strideCount++;
  }

  if(predictor.strideCount >= 10)
  {
    predictor.strideTable.clear();
    predictor.strideCount = 0;
  }
  }

  else
  {
    predictor.strideTable.push_back({actual_addr, 0, actual_value});
  }

  // If we have value predictions waiting for corresponding update
  if(inflight.size() && seq_no == inflight.back().seqNum)
  {  	   
    // It is now safe to update the FCM value history
    predictor.firstLevelTable[inflight.back().firstLevelIndex].commitTimeIndex ^= actual_value;
	uint64_t commitTimeSecondLevelIndex = 
	  predictor.firstLevelTable[inflight.back().firstLevelIndex].commitTimeIndex & predictor.firstLevelMask;
  
    // If there are not other predictions corresponding to this PC in flight in the pipeline, 
    // we make sure the FCM value history used to predict is clean by copying the architectural value
    // history in it.
    // Speculative FCM value history can become state when it is updated in speculativeUpdate() with a prediction
    // that we don't know the outcome of yet, because it was not used to speculate.
    if(--predictor.firstLevelTable[inflight.back().firstLevelIndex].inflight == 0)
    {
      predictor.firstLevelTable[inflight.back().firstLevelIndex].predictTimeIndex = 
        predictor.firstLevelTable[inflight.back().firstLevelIndex].commitTimeIndex;
    }
  
    if(predictor.secondLevelTable[commitTimeSecondLevelIndex].pred == actual_value)
	{
	     predictor.secondLevelTable[commitTimeSecondLevelIndex].conf = 
	       std::min(predictor.secondLevelTable[commitTimeSecondLevelIndex].conf + 1, 15);
    }
    else if(predictor.secondLevelTable[commitTimeSecondLevelIndex].conf == 0)
      predictor.secondLevelTable[commitTimeSecondLevelIndex].pred = actual_value;
    else
      predictor.secondLevelTable[commitTimeSecondLevelIndex].conf = 0;
		
    inflight.pop_back();
  }

  // It is now safe to update the address history register
  //if(insn == loadInstClass || insn == storeInstClass) 
  if(actual_addr != 0xdeadbeef)
   addrHist = (addrHist << 4) | actual_addr;
}

void beginPredictor(int argc_other, char **argv_other) {
   if (argc_other > 0)
      printf("CONTESTANT ARGUMENTS:\n");

   for (int i = 0; i < argc_other; i++)
      printf("\targv_other[%d] = %s\n", i, argv_other[i]);
}

void endPredictor() {
	printf("CONTESTANT OUTPUT--------------------------\n");
	printf("--------------------------\n");
}
