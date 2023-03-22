#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "Core/HW/GCPad.h"
#include "Core/Movie.h"
#include "InputCommon/GCPadStatus.h"
#include "InputCommon/InputConfig.h"
#include "Core/Scripting/ScriptContext.h"
#include "Core/Scripting/HelperClasses/ArgHolder.h"
#include "Core/Scripting/HelperClasses/ClassMetadata.h"

namespace Scripting::GameCubeControllerApi
{
extern const char* class_name;

extern std::array<bool, 4> overwrite_controller_at_specified_port;
extern std::array<Movie::ControllerState, 4> new_controller_inputs;
extern std::array<Movie::ControllerState, 4> controller_inputs_on_last_frame;
extern int current_controller_number_polled;

ClassMetadata GetClassMetadataForVersion(const std::string& api_version);
ClassMetadata GetAllClassMetadata();

ArgHolder GetCurrentPortNumberOfPoll(ScriptContext* current_script,
                                     std::vector<ArgHolder>& args_list);
ArgHolder SetInputsForPoll(ScriptContext* current_script, std::vector<ArgHolder>& args_list);
ArgHolder GetInputsForPoll(ScriptContext* current_script, std::vector<ArgHolder>& args_list);
ArgHolder GetInputsForPreviousFrame(ScriptContext* current_script,
                                    std::vector<ArgHolder>& args_list);
ArgHolder IsGcControllerInPort(ScriptContext* current_script, std::vector<ArgHolder>& args_list);
ArgHolder IsUsingPort(ScriptContext* current_script, std::vector<ArgHolder>& args_list);

}  // namespace Scripting::GameCubeControllerAPI
