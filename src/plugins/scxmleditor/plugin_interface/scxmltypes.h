// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QList>
#include <QVariant>

namespace ScxmlEditor {

namespace PluginInterface {

enum TagType {
    UnknownTag = 0,
    Metadata,
    MetadataItem,
    Scxml,
    State,
    Parallel,
    Transition,
    InitialTransition,
    Initial,
    Final,
    OnEntry,
    OnExit,
    History,
    Raise,
    If,
    ElseIf,
    Else,
    Foreach,
    Log,
    DataModel,
    Data,
    Assign,
    Donedata,
    Content,
    Param,
    Script,
    Send,
    Cancel,
    Invoke,
    Finalize
};

struct scxmltag_attribute_t;
struct scxmltag_type_t;

struct scxmltag_attribute_t
{
    const char *name; // scxml attribute name
    const char *value; // default value
    bool required;
    bool editable;
    int datatype;
};

struct scxmltag_type_t
{
    const char *name; // scxml output name
    bool canIncludeContent;
    const scxmltag_attribute_t *attributes;
    int n_attributes;
};

// Define tag-attributes
const scxmltag_attribute_t scxml_scxml_attributes[] = {
    {"initial", nullptr, false, false, QMetaType::QString},
    {"name", nullptr, false, true, QMetaType::QString},
    {"xmlns", "http://www.w3.org/2005/07/scxml", true, false, QMetaType::QString},
    {"version", "1.0", true, false, QMetaType::QString},
    {"datamodel", nullptr, false, true, QMetaType::QString},
    {"binding", "early;late", false, true, QMetaType::QStringList}
};

const scxmltag_attribute_t scxml_state_attributes[] = {
    {"id", nullptr, false, true, QMetaType::QString},
    {"initial", nullptr, false, false, QMetaType::QString}
};

const scxmltag_attribute_t scxml_parallel_attributes[] = {
    {"id", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_transition_attributes[] = {
    {"event", nullptr, false, true, QMetaType::QString},
    {"cond", nullptr, false, true, QMetaType::QString},
    {"target", nullptr, false, true, QMetaType::QString},
    {"type", "internal;external", false, true, QMetaType::QStringList}
};

const scxmltag_attribute_t scxml_initialtransition_attributes[] = {
    {"target", nullptr, false, false, QMetaType::QString}
};

const scxmltag_attribute_t scxml_final_attributes[] = {
    {"id", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_history_attributes[] = {
    {"id", nullptr, false, true, QMetaType::QString},
    {"type", "shallow;deep", false, true, QMetaType::QStringList}
};

const scxmltag_attribute_t scxml_raise_attributes[] = {
    {"event", nullptr, true, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_if_attributes[] = {
    {"cond", nullptr, true, true, QMetaType::QString},
};

const scxmltag_attribute_t scxml_elseif_attributes[] = {
    {"cond", nullptr, true, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_foreach_attributes[] = {
    {"array", nullptr, true, true, QMetaType::QString},
    {"item", nullptr, true, true, QMetaType::QString},
    {"index", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_log_attributes[] = {
    {"label", "", false, true, QMetaType::QString},
    {"expr", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_data_attributes[] = {
    {"id", nullptr, true, true, QMetaType::QString},
    {"src", nullptr, false, true, QMetaType::QString},
    {"expr", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_assign_attributes[] = {
    {"location", nullptr, true, true, QMetaType::QString},
    {"expr", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_content_attributes[] = {
    {"expr", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_param_attributes[] = {
    {"name", nullptr, true, true, QMetaType::QString},
    {"expr", nullptr, false, true, QMetaType::QString},
    {"location", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_script_attributes[] = {
    {"src", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_send_attributes[] = {
    {"event", nullptr, false, true, QMetaType::QString},
    {"eventexpr", nullptr, false, true, QMetaType::QString},
    {"target", nullptr, false, true, QMetaType::QString},
    {"targetexpr", nullptr, false, true, QMetaType::QString},
    {"type", nullptr, false, true, QMetaType::QString},
    {"typeexpr", nullptr, false, true, QMetaType::QString},
    {"id", nullptr, false, true, QMetaType::QString},
    {"idlocation", nullptr, false, true, QMetaType::QString},
    {"delay", nullptr, false, true, QMetaType::QString},
    {"delayexpr", nullptr, false, true, QMetaType::QString},
    {"namelist", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_cancel_attributes[] = {
    {"sendid", nullptr, false, true, QMetaType::QString},
    {"sendidexpr", nullptr, false, true, QMetaType::QString}
};

const scxmltag_attribute_t scxml_invoke_attributes[] = {
    {"type", nullptr, false, true, QMetaType::QString},
    {"typeexpr", nullptr, false, true, QMetaType::QString},
    {"src", nullptr, false, true, QMetaType::QString},
    {"srcexpr", nullptr, false, true, QMetaType::QString},
    {"id", nullptr, false, true, QMetaType::QString},
    {"idlocation", nullptr, false, true, QMetaType::QString},
    {"namelist", nullptr, false, true, QMetaType::QString},
    {"autoforward", ";true;false", false, true, QMetaType::QStringList}
};

const scxmltag_type_t scxml_unknown = {
    "unknown",
    true,
    nullptr,
    0
};

const scxmltag_type_t scxml_metadata = {
    "metadata",
    true,
    nullptr,
    0
};

const scxmltag_type_t scxml_metadataitem = {
    "item",
    true,
    nullptr,
    0
};

const scxmltag_type_t scxml_scxml = {
    "scxml",
    false,
    scxml_scxml_attributes,
    sizeof(scxml_scxml_attributes) / sizeof(scxml_scxml_attributes[0])
};

const scxmltag_type_t scxml_state = {
    "state",
    false,
    scxml_state_attributes,
    sizeof(scxml_state_attributes) / sizeof(scxml_state_attributes[0])
};

const scxmltag_type_t scxml_parallel = {
    "parallel",
    false,
    scxml_parallel_attributes,
    sizeof(scxml_parallel_attributes) / sizeof(scxml_parallel_attributes[0])
};

const scxmltag_type_t scxml_transition = {
    "transition",
    false,
    scxml_transition_attributes,
    sizeof(scxml_transition_attributes) / sizeof(scxml_transition_attributes[0])
};

const scxmltag_type_t scxml_initialtransition = {
    "transition",
    false,
    scxml_initialtransition_attributes,
    sizeof(scxml_initialtransition_attributes) / sizeof(scxml_initialtransition_attributes[0])
};

const scxmltag_type_t scxml_initial = {
    "initial",
    false,
    nullptr,
    0
};

const scxmltag_type_t scxml_final = {
    "final",
    false,
    scxml_final_attributes,
    sizeof(scxml_final_attributes) / sizeof(scxml_final_attributes[0])
};

const scxmltag_type_t scxml_onentry = {
    "onentry",
    false,
    nullptr,
    0
};

const scxmltag_type_t scxml_onexit = {
    "onexit",
    false,
    nullptr,
    0
};

const scxmltag_type_t scxml_history = {
    "history",
    false,
    scxml_history_attributes,
    sizeof(scxml_history_attributes) / sizeof(scxml_history_attributes[0])
};

const scxmltag_type_t scxml_raise = {
    "raise",
    false,
    scxml_raise_attributes,
    sizeof(scxml_raise_attributes) / sizeof(scxml_raise_attributes[0])
};

const scxmltag_type_t scxml_if = {
    "if",
    false,
    scxml_if_attributes,
    sizeof(scxml_if_attributes) / sizeof(scxml_if_attributes[0])
};

const scxmltag_type_t scxml_elseif = {
    "elseif",
    false,
    scxml_elseif_attributes,
    sizeof(scxml_elseif_attributes) / sizeof(scxml_elseif_attributes[0])
};

const scxmltag_type_t scxml_else = {
    "else",
    false,
    nullptr,
    0
};

const scxmltag_type_t scxml_foreach = {
    "foreach",
    false,
    scxml_foreach_attributes,
    sizeof(scxml_foreach_attributes) / sizeof(scxml_foreach_attributes[0])
};

const scxmltag_type_t scxml_log = {
    "log",
    false,
    scxml_log_attributes,
    sizeof(scxml_log_attributes) / sizeof(scxml_log_attributes[0])
};

const scxmltag_type_t scxml_datamodel = {
    "datamodel",
    false,
    nullptr,
    0
};

const scxmltag_type_t scxml_data = {
    "data",
    false,
    scxml_data_attributes,
    sizeof(scxml_data_attributes) / sizeof(scxml_data_attributes[0])
};

const scxmltag_type_t scxml_assign = {
    "assign",
    false,
    scxml_assign_attributes,
    sizeof(scxml_assign_attributes) / sizeof(scxml_assign_attributes[0])
};

const scxmltag_type_t scxml_donedata = {
    "donedata",
    false,
    nullptr,
    0
};

const scxmltag_type_t scxml_content = {
    "content",
    false,
    scxml_content_attributes,
    sizeof(scxml_content_attributes) / sizeof(scxml_content_attributes[0])
};

const scxmltag_type_t scxml_param = {
    "param",
    false,
    scxml_param_attributes,
    sizeof(scxml_param_attributes) / sizeof(scxml_param_attributes[0])
};

const scxmltag_type_t scxml_script = {
    "script",
    true,
    scxml_script_attributes,
    sizeof(scxml_script_attributes) / sizeof(scxml_script_attributes[0])
};

const scxmltag_type_t scxml_send = {
    "send",
    false,
    scxml_send_attributes,
    sizeof(scxml_send_attributes) / sizeof(scxml_send_attributes[0])
};

const scxmltag_type_t scxml_cancel = {
    "cancel",
    false,
    scxml_cancel_attributes,
    sizeof(scxml_cancel_attributes) / sizeof(scxml_cancel_attributes[0])
};

const scxmltag_type_t scxml_invoke = {
    "invoke",
    false,
    scxml_invoke_attributes,
    sizeof(scxml_invoke_attributes) / sizeof(scxml_invoke_attributes[0])
};

const scxmltag_type_t scxml_finalize = {
    "finalize",
    false,
    nullptr,
    0
};

const scxmltag_type_t scxml_tags[] = {
    scxml_unknown,
    scxml_metadata,
    scxml_metadataitem,
    scxml_scxml,
    scxml_state,
    scxml_parallel,
    scxml_transition,
    scxml_initialtransition,
    scxml_initial,
    scxml_final,
    scxml_onentry,
    scxml_onexit,
    scxml_history,
    scxml_raise,
    scxml_if,
    scxml_elseif,
    scxml_else,
    scxml_foreach,
    scxml_log,
    scxml_datamodel,
    scxml_data,
    scxml_assign,
    scxml_donedata,
    scxml_content,
    scxml_param,
    scxml_script,
    scxml_send,
    scxml_cancel,
    scxml_invoke,
    scxml_finalize
};

} // namespace PluginInterface
} // namespace ScxmlEditor
