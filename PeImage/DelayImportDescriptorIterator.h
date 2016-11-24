#pragma once
#include <Windows.h>
#include <delayimp.h>
#include "DelayImportDescriptor.h"
#include "ImportThunk.h"
#include "IteratorBase.h"
namespace PeDecoder
{
	class DelayImportDirectory;
	class DelayImportDescriptorIterator :
		public IteratorBase<
		DelayImportDescriptorIterator,
		_STD forward_iterator_tag,
		_STD unique_ptr<DelayImportDescriptor>,
		ptrdiff_t,
		_STD unique_ptr<DelayImportDescriptor>,
		_STD unique_ptr<DelayImportDescriptor>>
	{
	public:
		friend IteratorFriendAccess;
		DelayImportDescriptorIterator(const DelayImportDirectory& delayImportDirectory, PImgDelayDescr ptr);

	private:
		bool Equal(const DelayImportDescriptorIterator & val) const;
		void Increment();
		reference Dereference();
		pointer AddressOf();
		const DelayImportDirectory* delayImportDirectory_;
		PImgDelayDescr dataPtr_;
	};
}  // namespace PeDecoder