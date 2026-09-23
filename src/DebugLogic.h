#ifndef __EXODUSMCP_DEBUGLOGIC_H__
#define __EXODUSMCP_DEBUGLOGIC_H__
// Pure helper logic used by the MCP tools, kept free of Exodus SDK dependencies so it can be unit-tested in isolation.
#include <vector>

namespace DebugLogic {

//----------------------------------------------------------------------------------------------------------------------
// Sprite link chain walk (Mega Drive VDP, mode 5)
//----------------------------------------------------------------------------------------------------------------------
enum class SpriteChainStop
{
	LinkZero,       // Last sprite emitted had link 0: normal end of the list
	LinkOutOfRange, // Last sprite emitted links to an entry >= the table size for the current mode (the VDP stops there)
	Loop,           // Last sprite emitted links back to an entry already visited
	MaxCount,       // The maximum sprite count for the current mode was reached
};

struct SpriteChainResult
{
	std::vector<unsigned int> order; // Table indices in the order the VDP visits them
	SpriteChainStop stop;
	unsigned int stopLink;           // Link value of the last emitted sprite
};

// Walks the sprite list the way the VDP does: start at entry 0, follow each entry's link field, and stop after
// emitting a sprite whose link is 0 or points outside the table (80 entries in H40, 64 in H32), or when tableSize
// sprites have been visited. A link back to an entry already visited stops the walk as well (loop protection).
// getLink(index) returns the 7-bit link field of the table entry at index.
template<class GetLink>
SpriteChainResult WalkSpriteChain(unsigned int tableSize, GetLink getLink)
{
	SpriteChainResult result;
	result.stop = SpriteChainStop::MaxCount;
	result.stopLink = 0;
	if (tableSize == 0)
		return result;

	std::vector<bool> visited(tableSize, false);
	unsigned int index = 0;
	while (result.order.size() < tableSize)
	{
		visited[index] = true;
		result.order.push_back(index);
		unsigned int link = getLink(index) & 0x7F;
		result.stopLink = link;
		if (link == 0)
		{
			result.stop = SpriteChainStop::LinkZero;
			return result;
		}
		if (link >= tableSize)
		{
			result.stop = SpriteChainStop::LinkOutOfRange;
			return result;
		}
		if (visited[link])
		{
			result.stop = SpriteChainStop::Loop;
			return result;
		}
		index = link;
	}
	result.stop = SpriteChainStop::MaxCount;
	return result;
}

//----------------------------------------------------------------------------------------------------------------------
// Disassembler: skipping a run of bytes that do not decode as instructions
//----------------------------------------------------------------------------------------------------------------------
enum class SkipStop
{
	ValidOpcode, // A valid instruction starts at endAddress
	ReadFailed,  // The opcode at endAddress could not be read
	EndOfSpace,  // The next step would go past the end of the address space
	Limit,       // maxBytes were skipped without finding a valid instruction
};

struct SkipResult
{
	unsigned int endAddress; // First address not skipped
	SkipStop stop;
};

// Advances from startAddress (which does not decode as an instruction) in steps of stepSize bytes (the processor's
// minimum opcode size: 2 on the 68000, 1 on the Z80) until a valid instruction is found, reading fails, the end of
// the address space (addressMask) is passed, or maxBytes have been skipped.
// probe(address) returns 1 for a valid instruction, 0 for invalid data, -1 if the opcode could not be read.
template<class Probe>
SkipResult SkipNonCode(unsigned int startAddress, unsigned int stepSize, unsigned int addressMask, unsigned int maxBytes, Probe probe)
{
	if (stepSize == 0)
		stepSize = 1;
	unsigned long long address = startAddress;
	const unsigned long long limit = (unsigned long long)startAddress + maxBytes;
	for (;;)
	{
		address += stepSize;
		if (address > addressMask)
			return SkipResult{ (unsigned int)address, SkipStop::EndOfSpace };
		if (address >= limit)
			return SkipResult{ (unsigned int)limit, SkipStop::Limit };
		int state = probe((unsigned int)address);
		if (state < 0)
			return SkipResult{ (unsigned int)address, SkipStop::ReadFailed };
		if (state > 0)
			return SkipResult{ (unsigned int)address, SkipStop::ValidOpcode };
	}
}

} // namespace DebugLogic

#endif
