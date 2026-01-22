"""Rule to generate and validate a SOME/IP gateway configuration"""

load("@score_communication//bazel/tools:json_schema_validator.bzl", "validate_json_schema_test")

def _generate_someip_config_bin_impl(ctx):
    """Generates a SOME/IP Gateway configuration binary based on a specified json file."""

    schema_filename = ctx.file._schema.basename
    bin_filename = schema_filename.replace(".fbs", ".bin")

    output_file = ctx.actions.declare_file(ctx.attr.output_filename)
    output_dir = output_file.dirname

    if not ctx.attr.output_filename.endswith(".bin"):
        fail("The output_file must end with '.bin'")

    commands = [
        "%s --binary" % ctx.executable._flatc.path,
        "-o %s" % output_dir,
        "%s" % ctx.file._schema.path,
        "%s" % ctx.file.json.path,
    ]

    if bin_filename != output_file.basename:
        rename_command = "&& mv %s/%s %s" % (output_dir, bin_filename, output_file.path)
        commands.append(rename_command)

    command = " ".join(commands)

    ctx.actions.run_shell(
        outputs = [output_file],
        inputs = [
            ctx.file.json,
            ctx.file._schema,
        ],
        tools = [ctx.executable._flatc],
        command = command,
        mnemonic = "SomeIpConfigBin",
        progress_message = "Generating binary config from {json}".format(json = ctx.file.json.path),
    )

    return [DefaultInfo(files = depset([output_file]))]

generate_someip_config_bin = rule(
    implementation = _generate_someip_config_bin_impl,
    attrs = {
        "json": attr.label(
            doc = "The input JSON configuration file.",
            mandatory = True,
            allow_single_file = True,
        ),
        "output_filename": attr.string(
            doc = "The name of the output binary file. Must end with '.bin'.",
            mandatory = True,
        ),
        "_schema": attr.label(
            doc = "The FlatBuffers schema file.",
            allow_single_file = True,
            default = "@score_someip_gateway//src/gatewayd:etc/gatewayd_config.fbs",
        ),
        "_flatc": attr.label(
            doc = "The FlatBuffers compiler.",
            default = "@flatbuffers//:flatc",
            cfg = "exec",
            executable = True,
        ),
    },
    doc = "Generates a SOME/IP Gateway configuration binary based on a specified json file.",
)

def _validate_someip_config_impl(name, json, _schema, expected_failure, **kwargs):
    """Validates a SOME/IP gateway config json against its corresponding schema."""

    validate_json_schema_test(
        name = name,
        json = json,
        schema = _schema,
        expected_failure = expected_failure,
        **kwargs
    )

validate_someip_config_test = macro(
    doc = "Validates a SOME/IP gateway config json against its corresponding schema.",
    implementation = _validate_someip_config_impl,
    attrs = {
        "json": attr.label(
            doc = "The input JSON configuration file.",
            mandatory = True,
            allow_single_file = True,
        ),
        "_schema": attr.label(
            doc = "The JSON schema file.",
            allow_single_file = True,
            default = "@score_someip_gateway//src/gatewayd:etc/gatewayd_config.schema.json",
        ),
        "expected_failure": attr.bool(default = False),
    },
)
