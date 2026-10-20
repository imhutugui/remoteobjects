#include <string>

#include "generator.h"
#include "parser.h"
#include "test.h"

using namespace rep;

namespace {

bool parseText(const std::string &text, File &file, std::string &error)
{
    Parser parser;
    if (!parser.parse(text, file, error)) {
        if (error.empty())
            error = parser.error();
        return false;
    }
    return true;
}

}  // namespace

RO_TEST(rep_parses_class_members)
{
    const std::string text = R"(
        class SimpleSwitch
        {
            PROP(bool currState=false);
            SLOT(void server_slot(bool clientState));
        };
    )";
    File file;
    std::string error;
    RO_CHECK(parseText(text, file, error));
    RO_CHECK_EQ(file.classes.size(), size_t{1});
    const auto &cls = file.classes[0];
    RO_CHECK_EQ(cls.name, std::string("SimpleSwitch"));
    RO_CHECK_EQ(cls.properties.size(), size_t{1});
    RO_CHECK_EQ(cls.properties[0].name, std::string("currState"));
    RO_CHECK_EQ(cls.properties[0].type.name, std::string("bool"));
    RO_CHECK_EQ(cls.properties[0].defaultValue, std::string("false"));
    RO_CHECK_EQ(cls.slots.size(), size_t{1});
    RO_CHECK_EQ(cls.slots[0].name, std::string("server_slot"));
    RO_CHECK_EQ(cls.slots[0].returnType.name, std::string("void"));
    RO_CHECK_EQ(cls.slots[0].arguments.size(), size_t{1});
    RO_CHECK_EQ(cls.slots[0].arguments[0].type.name, std::string("bool"));
}

RO_TEST(rep_parses_signals_multiple_arguments_and_comments)
{
    const std::string text = R"(
        // leading comment
        class Engine
        {
            /* block comment */
            SIGNAL(stateChanged(int code, QString message));
            SIGNAL(empty());
        }
    )";
    File file;
    std::string error;
    RO_CHECK(parseText(text, file, error));
    const auto &cls = file.classes[0];
    RO_CHECK_EQ(cls.signals.size(), size_t{2});
    RO_CHECK_EQ(cls.signals[0].name, std::string("stateChanged"));
    RO_CHECK_EQ(cls.signals[0].arguments.size(), size_t{2});
    RO_CHECK_EQ(cls.signals[0].arguments[1].type.name, std::string("QString"));
    RO_CHECK_EQ(cls.signals[1].arguments.size(), size_t{0});
}

RO_TEST(rep_parses_enums_pods_and_templates)
{
    const std::string text = R"(
        #include <QString>
        ENUM Test {TRUE, FALSE}
        ENUM Kind : int {A = 1, B = 2};
        POD PodT(QList<TestEnum::Test> t)
        POD PresetInfo(int presetNumber, float frequency, QString stationName)
        class Container
        {
            ENUM EngineType {Null, Gas};
            PROP(QMap<QString, QString> labels);
            PROP(int cylinders = 4);
        }
    )";
    File file;
    std::string error;
    RO_CHECK(parseText(text, file, error));
    RO_CHECK_EQ(file.enums.size(), size_t{2});
    RO_CHECK_EQ(file.enums[0].name, std::string("Test"));
    RO_CHECK_EQ(file.enums[0].values.size(), size_t{2});
    RO_CHECK_EQ(file.enums[1].underlyingType, std::string("int"));
    RO_CHECK_EQ(file.pods.size(), size_t{2});
    RO_CHECK_EQ(file.pods[0].name, std::string("PodT"));
    RO_CHECK_EQ(file.pods[0].fields[0].type.name, std::string("QList<TestEnum::Test>"));
    RO_CHECK_EQ(file.pods[1].fields.size(), size_t{3});
    RO_CHECK_EQ(file.classes[0].enums.size(), size_t{1});
    RO_CHECK_EQ(file.classes[0].properties[0].type.name, std::string("QMap<QString,QString>"));
    RO_CHECK_EQ(file.classes[0].properties[1].defaultValue, std::string("4"));
}

RO_TEST(rep_reports_unsupported_types)
{
    const std::string text = "class Foo { SIGNAL(changed(QTime t)); };";
    File file;
    std::string error;
    RO_CHECK(parseText(text, file, error));
    std::string output;
    RO_CHECK(!generate(file, "foo.rep", output, error));
    RO_CHECK(error.find("QTime") != std::string::npos);
}

RO_TEST(rep_signature_changes_with_interface)
{
    File first;
    File second;
    std::string error;
    RO_CHECK(parseText("class Foo { PROP(int a); };", first, error));
    RO_CHECK(parseText("class Foo { PROP(int a); };", second, error));
    RO_CHECK_EQ(computeSignature(first), computeSignature(second));

    File third;
    RO_CHECK(parseText("class Foo { PROP(int b); };", third, error));
    RO_CHECK(computeSignature(first) != computeSignature(third));
}

RO_TEST(rep_generates_source_and_replica)
{
    File file;
    std::string error;
    RO_CHECK(parseText(R"(
        POD Info(int id, QString name)
        ENUM Mode {Off, On}
        class Device
        {
            ENUM Kind {A, B};
            PROP(bool enabled = true);
            PROP(Info info);
            PROP(Mode mode = Off);
            PROP(const int &count READONLY);
            SIGNAL(changed(Info info, int version));
            SLOT(int ping(int payload));
            SLOT(void reset());
        };
    )",
                      file, error));

    std::string output;
    RO_CHECK(generate(file, "device.rep", output, error));

    const std::string expected[] = {
        "class DeviceSimpleSource : public ro::Source",
        "class DeviceReplica : public ro::Replica",
        "struct Info {",
        "enum class Mode : int32_t",
        "bool enabled() const",
        "void setEnabled(bool value)",
        "int32_t count() const",
        "virtual int32_t ping(int32_t payload) = 0;",
        "ro::PendingCall ping(int32_t payload)",
        "registerProperty(\"info\"",
        "registerSignal([this](const ro::Value::List &args)",
        "::Mode mode_ = Mode::Off;",
        "Kind",
    };
    for (const auto &needle : expected)
        RO_CHECK(output.find(needle) != std::string::npos);
    // read-only properties must not get a setter
    RO_CHECK(output.find("void setCount") == std::string::npos);
}
