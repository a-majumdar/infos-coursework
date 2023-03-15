
#include <infos/mm/page-allocator.h>
#include <infos/mm/mm.h>
#include <infos/kernel/kernel.h>
#include <infos/kernel/log.h>
#include <infos/util/math.h>
#include <infos/util/printf.h>

using namespace infos::kernel;
using namespace infos::mm;
using namespace infos::util;

#define MAX_ORDER 18

/**
 * A buddy page allocation algorithm.
 */
class BuddyPageAllocator : public PageAllocatorAlgorithm
{
private:
	/** 
	 * Given a page descriptor, and an order, returns the buddy PGD.  The buddy could either be
	 * to the left or the right of PGD, in the given order.
	 * @param pgd The page descriptor to find the buddy for.
	 * @param order The order in which the page descriptor lives.
	 * @return Returns the buddy of the given page descriptor, in the given order.
	 */
	PageDescriptor *buddy_of(PageDescriptor *pgd, int order)
	{
		if (!alignment(pgd, order)) { return nullptr; } // order within bounds also checked

		int buddy;
		pfn_t pfn = sys.mm().pgalloc().pgd_to_pfn(pgd);
		int block = blockPages(order);

		buddy = alignment(pgd, order + 1) ? pfn + block : pfn - block;

		// mm_log.messagef(LogLevel::DEBUG, "in buddy_of, buddy of %d is %d", pgd, buddy);
		return sys.mm().pgalloc().pfn_to_pgd(buddy);

	}

	/**
	 * Given a pointer to a block of free memory in the order "source_order", this function will
	 * split the block in half, and insert it into the order below.
	 * @param block_pointer A pointer to a pointer containing the beginning of a block of free memory.
	 * @param source_order The order in which the block of free memory exists.  Naturally,
	 * the split will insert the two new blocks into the order below.
	 * @return Returns the left-hand-side of the new block.
	 */
	PageDescriptor *split_block(PageDescriptor **block_pointer, int source_order)
	{
		// assert(source_order <= MAX_ORDER && source_order > 0);
		// mm_log.messagef(LogLevel::DEBUG, "in split_block");
		PageDescriptor *original = *block_pointer;
		if ( !alignment(*block_pointer, source_order) || original->type != PageDescriptorType::AVAILABLE ) { return nullptr; } // order within bounds also checked


		int lOrder = source_order - 1;
		PageDescriptor *other = buddy_of(original, lOrder);

		removeFree(original, source_order);
		insertFree(original, lOrder);
		insertFree(other, lOrder);

		return original;
	}

	/**
	 * Takes a block in the given source order, and merges it (and its buddy) into the next order.
	 * @param block_pointer A pointer to a pointer containing a block in the pair to merge.
	 * @param source_order The order in which the pair of blocks live.
	 * @return Returns the new slot that points to the merged block.
	 */
	PageDescriptor *merge_block(PageDescriptor **block_pointer, int source_order)
	{
		// mm_log.messagef(LogLevel::DEBUG, "in merge_block");
		assert(source_order <= MAX_ORDER - 1 && source_order >= 0);
		if ( !alignment(*block_pointer, source_order) ) { return nullptr; } // order within bounds also checked

		int nOrder = source_order + 1;
		PageDescriptor *original = *block_pointer;
		PageDescriptor *other = buddy_of(original, source_order);

		removeFree(original, source_order);
		removeFree(other, source_order);

		//if page descriptor is pointing at left or right of the pair
		if (alignment(original, nOrder))
		{
			insertFree(original, nOrder);
			return original;
		}
		else if (alignment(other, nOrder))
		{
			insertFree(other, nOrder);
			return other;
		}

		return nullptr;
	}

	/**
	 * 
	 * Takes a page descpritor and searches for all the possible merges for it 
	 * @param pgd A pointer to a page descriptor where we should merge towards
	 * @param order The order in which the page descriptor exists
	 */

	void mergingOfPgd(PageDescriptor *pgd, int order)
	{
		mm_log.messagef(LogLevel::DEBUG, "in mergingOfPgd");
		assert(order < MAX_ORDER && order >= 0);

		int temp_order = order;
		PageDescriptor *merge_pgd;
		PageDescriptor *buddy_pgd = buddy_of(pgd, order);
		PageDescriptor *free_pgd = _free_areas[order];

		//iterating through for every order above current one so that it merges the complete way up for given page descriptor
		for (int temp_order = order; temp_order < MAX_ORDER; temp_order++)
		{
			while (free_pgd != NULL)
			{
				if (buddy_pgd != free_pgd)
				{
					free_pgd = free_pgd->next_free;
				}
				else if (buddy_pgd == free_pgd)
				{
					//As the buddy pgd is within the free pgd, they are both free to be merged
					PageDescriptor **block_pointer = &buddy_pgd;
					merge_pgd = merge_block(block_pointer, temp_order);
					temp_order++;
					buddy_pgd = buddy_of(merge_pgd, temp_order);
					if (buddy_pgd != NULL)
					{
						return;
					}
					else
					{
						free_pgd = _free_areas[temp_order];
						temp_order--;
					}
				}
				else
				{
					return;
				}
			}
		}
	}

	/**
     * Continuously splits page descriptor block untill it is of the smallest denomination
     * @param start page descriptor indicatin start of block where the order 0 block will now encompass
     */
	void splitUntillPoint(PageDescriptor *start)
	{
		// mm_log.messagef(LogLevel::DEBUG, "in splitUntillPoint");
		PageDescriptor *usedPgd;
		int searchingOrder = 0;

		//Iterating through every order within the free_area to locate and split towards page descriptor
		for (int searchingOrder = 0; searchingOrder <= MAX_ORDER; searchingOrder++)
		{
			PageDescriptor *pgd = _free_areas[searchingOrder];
			while (pgd != NULL)
			{
				int pfn = sys.mm().pgalloc().pgd_to_pfn(pgd);
				int intStart = sys.mm().pgalloc().pgd_to_pfn(start);
				int endOfBlock = pfn + blockPages(searchingOrder);
				if (intStart < endOfBlock && start >= pgd)
				{
					usedPgd = pgd;
					for (int splt = searchingOrder; splt > 0; splt--)
					{
						usedPgd = split_block(&usedPgd, splt);
					}
					return;
				}
				else
				{
					pgd = pgd->next_free;
				}
			}
		}
		mm_log.messagef(LogLevel::ERROR, "not found");
		return;
	}

	/**
     * returns the integer value of the number of pages within an order 
     * @param order 2 to the power of order indicates number of pages
	 * @return integer value indiciating number of pages
     */
	static int blockPages(int order)
	{
		// mm_log.messagef(LogLevel::DEBUG, "in blockPages");
		checkOrder(order);
		// mm_log.messagef(LogLevel::DEBUG, "order is %d and block is %d", order, 1 << order);
		return 1 << order;
	}

	static void checkOrder(int order)
	{
		assert(order <= MAX_ORDER && order >=0);
	}

	/**
	 * returns a boolean value indicating if page frame number is perfectly divisble by the expected number for its order
	 * hence aligned
     * @param pgd page descriptor that should be aligned to start of block
     * @param order order of page descrpitor
	 * @return true if page descriptor is correctly aligned, false otherwise
	 * 
     */
	static bool alignment(const PageDescriptor *pgd, int order)
	{
		// mm_log.messagef(LogLevel::DEBUG, "in alignment");
		checkOrder(order); //checking if order is within range
		int remainder = (sys.mm().pgalloc().pgd_to_pfn(pgd) % blockPages(order));
		return (remainder == 0); //if remainder is 0 then it is aligned properly
	}

	/**
	 * frees a block of pages
	 * @param pgd The page descriptor of the block with which we are inserting 
	 * @param order The order in which we are inserting the block 
	 */

	void insertFree(PageDescriptor *pgd, int order)
	{
		// mm_log.messagef(LogLevel::DEBUG, "in insertFree");
		PageDescriptor **position = &_free_areas[order];

		while (*position != NULL)
		{
			if (pgd == *position) { position = &(*position)->next_free; }
			else { break; }
		}

		pgd->next_free = *position;
		*position = pgd;
	}

	/**
	 * Removes a block from free list of pages
	 * @param pgd The page descriptor of the block we are removing
	 * @param order The order in which we are removing the block
	 */
	void removeFree(PageDescriptor *pgd, int order)
	{
		// mm_log.messagef(LogLevel::DEBUG, "in removeFree");
		PageDescriptor **slot = &_free_areas[order];

		while (*slot != NULL && pgd != *slot)
		{
			if (pgd != *slot) {	slot = &(*slot)->next_free; }
			else { break; }
		}

		*slot = pgd->next_free;
		pgd->next_free = nullptr;
	}

public:
	/**
	 * Allocates block of contiguous pages
	 * @param order The power of two, of the number of contiguous pages to allocate.
	 * @return Returns a pointer to the first page descriptor for the newly allocated page range, or NULL if
	 * allocation failed.
	 */
	PageDescriptor *allocate_pages(int order) override
	{
		// mm_log.messagef(LogLevel::DEBUG, "in allocate_pages");
		assert(order <= MAX_ORDER && order >= 0);
		int temp_order = order;
		for (int i = temp_order; i <= MAX_ORDER; i++)
		{
			if (_free_areas[temp_order] == NULL)
			{
				temp_order++;
			}
		}

		//must split top down so going back down to original order value and splitting all the way
		PageDescriptor *pgd = _free_areas[temp_order];
		for (int i = temp_order; i > order; i--)
		{
			if (i !=0)
			{
				pgd = split_block(&pgd, i);
			}
		}
		removeFree(pgd, order);
		return pgd;
	}

	/**
	 * Frees 2^order contiguous pages.
	 * @param pgd A pointer to an array of page descriptors to be freed.
	 * @param order The power of two number of contiguous pages to free.
	 */
	void free_pages(PageDescriptor *pgd, int order) override
	{
		// mm_log.messagef(LogLevel::DEBUG, "in free_pages");
		// assert(order <= MAX_ORDER && order >= 0);
		assert(alignment(pgd, order));

		insertFree(pgd, order);
		if (order < MAX_ORDER)
		{
			mergingOfPgd(pgd, order);
		}
	}

	/**
     * Marks a range of pages as available for allocation.
     * @param start A pointer to the first page descriptors to be made available.
     * @param count The number of page descriptors to make available.
     */
	virtual void insert_page_range(PageDescriptor *start, uint64_t count) override
	{
		// mm_log.messagef(LogLevel::DEBUG, "in insert_page_range");
		int order_Count = MAX_ORDER;
		while (count > 0)
		{
			//returning the highest possible order that is also page alligned so as to free as many pages as possible at once
			while (blockPages(order_Count) > count || !alignment(start, order_Count))
			{
				order_Count--;
			}
			assert(alignment(start, order_Count));
			insertFree(start, order_Count);
			start = start + blockPages(order_Count);
			count = count - blockPages(order_Count);
			order_Count = MAX_ORDER;
		}
		return;
	}

	/**
     * Marks a range of pages as unavailable for allocation.
     * @param start A pointer to the first page descriptors to be made unavailable.
     * @param count The number of page descriptors to make unavailable.
     */
	virtual void remove_page_range(PageDescriptor *start, uint64_t count) override
	{
		mm_log.messagef(LogLevel::DEBUG, "in remove_page_range, count = %d", count);
		while (count > 1)
		{
			splitUntillPoint(start);
			removeFree(start, 0);

			start++;
			count--;
		}
		start++;
		removeFree(start, 0);

		//Runs through free list and checks for any possible merges and performs them at ever level
		for (unsigned i = 0; i < MAX_ORDER - 1; i++)
		{
			PageDescriptor *pgd = _free_areas[i];
			while (pgd != NULL)
			{
				mergingOfPgd(pgd, i);
				pgd = pgd->next_free;
			}
		}
	}

	/**
	 * Initialises the allocation algorithm.
	 * @return Returns TRUE if the algorithm was successfully initialised, FALSE otherwise.
	 */
	bool init(PageDescriptor *page_descriptors, uint64_t nr_page_descriptors) override
	{
		_free_areas[MAX_ORDER - 1] = page_descriptors;
		return (nr_page_descriptors > 0 && page_descriptors != NULL);
	}

	/**
	 * Returns the friendly name of the allocation algorithm, for debugging and selection purposes.
	 */
	const char *name() const override { return "buddy"; }

	/**
	 * Dumps out the current state of the buddy system
	 */
	void dump_state() const override
	{
		// Print out a header, so we can find the output in the logs.
		mm_log.messagef(LogLevel::DEBUG, "BUDDY STATE:");

		// Iterate over each free area.
		for (unsigned int i = 0; i < ARRAY_SIZE(_free_areas); i++)
		{
			char buffer[256];
			snprintf(buffer, sizeof(buffer), "[%d] ", i);

			// Iterate over each block in the free area.
			PageDescriptor *pg = _free_areas[i];
			while (pg)
			{
				// Append the PFN of the free block to the output buffer.
				snprintf(buffer, sizeof(buffer), "%s%lx ", buffer, sys.mm().pgalloc().pgd_to_pfn(pg));
				pg = pg->next_free;
			}

			mm_log.messagef(LogLevel::DEBUG, "%s", buffer);
		}
	}

private:
	PageDescriptor *_free_areas[MAX_ORDER + 1];
};


/*
 * Allocation algorithm registration framework
 */
RegisterPageAllocator(BuddyPageAllocator);