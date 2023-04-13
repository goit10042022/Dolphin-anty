#include "Core/PowerPC/PowerPC.h"
#include "Core/Core.h"
#include <vector>

class InstructionBreakpointsHolder {
  public:
    InstructionBreakpointsHolder() {}
   inline ~InstructionBreakpointsHolder() { ClearAllBreakpoints(); }

    void AddBreakpoint(u32 addr)
    {
      breakpoint_addresses.push_back(addr);  // add this to the list of breakpoints regardless of
                                             // whether or not its a duplicate

      //Only add the breakpoint to PowerPC if it's not in the list of breakpoints yet.
      if (!this->ContainsBreakpoint(addr))
      {
        Core::QueueHostJob(
            [=] { PowerPC::breakpoints.Add(addr, false, false, false, std::nullopt); }, true);
      }
    }

    void RemoveBreakpoint(u32 addr)
    {
      size_t num_copies = this->GetNumCopiesOfBreakpoint(addr);
      if (num_copies == 0)
        return;
      else
      {
        breakpoint_addresses.erase(std::find(breakpoint_addresses.begin(), breakpoint_addresses.end(), addr));
        if (num_copies == 1)
        {
          Core::QueueHostJob([=] { PowerPC::breakpoints.Remove(addr); }, true);
        }
      }
    }

      void ClearAllBreakpoints()
      {
        while (breakpoint_addresses.size() > 0)
          this->RemoveBreakpoint(breakpoint_addresses[0]);
      }

      bool ContainsBreakpoint(u32 addr)
      {
        return this->GetNumCopiesOfBreakpoint(addr) > 0;
      }

    private:
      size_t GetNumCopiesOfBreakpoint(u32 addr)
      {
        return std::count(breakpoint_addresses.begin(), breakpoint_addresses.end(), addr);
      }

      std::vector<u32> breakpoint_addresses;
};
