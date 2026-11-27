#include "Script/Runtime/ScriptState.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm::script {

std::shared_ptr<GlobalState> GlobalState::create(std::shared_ptr<const Program> program)
{
    std::shared_ptr<GlobalState> state(new GlobalState());
    if (program) {
        state->values_ = program->globalInitialValues();
    }
    state->program_ = std::move(program);
    return state;
}

const Value* GlobalState::find(std::string_view name) const
{
    if (!program_) return nullptr;
    const Module& module = program_->module();
    for (std::size_t g = 0; g < module.globals.size(); ++g) {
        if (module.string(module.globals[g].nameIndex) == name) return &values_[g];
    }
    return nullptr;
}

Result<std::shared_ptr<BehaviourInstance>> BehaviourInstance::create(std::shared_ptr<GlobalState> globals,
                                                                   std::uint32_t behaviour, Value self)
{
    if (!globals || !globals->program()) {
        return makeError(ErrorCode::InvalidArgument, "A behaviour instance needs the global state of its program");
    }
    const Module& module = globals->program()->module();
    if (behaviour >= module.behaviours.size()) {
        return makeError(ErrorCode::InvalidArgument, concat("Script '", globals->program()->name(), "' has no behaviour ", behaviour));
    }
    const BehaviourKind kind = module.behaviours[behaviour].kind;
    const ScriptType expected = kind == BehaviourKind::Actor    ? ScriptType::Actor
                                : kind == BehaviourKind::Object ? ScriptType::Object
                                                                : ScriptType::None;
    if (!self.is(expected) || (expected != ScriptType::None && self.asReference().isNull())) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("A ", toString(kind), " behaviour instance needs ",
                                expected == ScriptType::None ? "no self" : concat("a non-null ", toString(expected), " as self"),
                                ", got ", toString(self.type())));
    }
    std::shared_ptr<BehaviourInstance> instance(new BehaviourInstance());
    instance->values_ = globals->program()->instanceInitialValues(behaviour);
    instance->globals_ = std::move(globals);
    instance->behaviour_ = behaviour;
    instance->self_ = std::move(self);
    return instance;
}

const Value* BehaviourInstance::find(std::string_view name) const
{
    const Module& module = program().module();
    const std::vector<VariableEntry>& variables = module.behaviours[behaviour_].variables;
    for (std::size_t v = 0; v < variables.size(); ++v) {
        if (module.string(variables[v].nameIndex) == name) return &values_[v];
    }
    return nullptr;
}

} // namespace schuimm::script
