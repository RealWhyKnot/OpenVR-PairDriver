#pragma once

#include <string>
#include <utility>

namespace openvr_pair::overlay {

class ProfileStoreBase
{
public:
	explicit ProfileStoreBase(std::wstring root) : root_(std::move(root)) {}
	virtual ~ProfileStoreBase() = default;

	const std::wstring &Root() const { return root_; }

private:
	std::wstring root_;
};

} // namespace openvr_pair::overlay
