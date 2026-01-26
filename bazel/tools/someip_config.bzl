"""Rule to generate and validate a SOME/IP gateway configuration"""

load("@score_communication//bazel/tools:json_schema_validator.bzl", "validate_json_schema_test")

def _generate_someip_config_bin_impl(name, json, output, **kwargs):
    """Generates a SOME/IP Gateway configuration binary based on a specified json file."""

    schema_file = "gatewayd_config.fbs"
    schema_file_path = "@score_someip_gateway//src/gatewayd:etc/" + schema_file

    expected_file_name = schema_file.replace(".fbs", ".bin")
    output_basename = output.split("/")[-1].split(":")[-1]

    if not output_basename.endswith(".bin"):
        fail("'output' must end with '.bin'")

    if "/" in output or ":" in output:
        path_part = output.split(":")[-1]
        if "/" in path_part:
            output_dir = "/".join(path_part.split("/")[:-1])
        else:
            output_dir = "."
    else:
        output_dir = "."

    commands = [
        "$(location @flatbuffers//:flatc) --binary -o $(RULEDIR)/%s $(SRCS)" % output_dir,
    ]

    if expected_file_name != output_basename:
        commands.append("&& mv $(@D)/etc/gatewayd_config.bin $(@)")

    native.genrule(
        name = name,
        srcs = [
            schema_file_path,
            json,
        ],
        outs = [output],
        cmd = " ".join(commands),
        message = "Generates a SOME/IP Gateway configuration binary based on a specified json file.",
        tools = ["@flatbuffers//:flatc"],
        **kwargs
    )

generate_someip_config_bin = macro(
    implementation = _generate_someip_config_bin_impl,
    inherit_attrs = native.genrule,  # type: ignore
    attrs = {
        "json": attr.label(
            doc = "The input JSON configuration file.",
            mandatory = True,
            allow_single_file = True,
            configurable = False,
        ),
        "output": attr.string(
            doc = "The output path (relative or absolute). Must end with '.bin'.",
            mandatory = True,
            configurable = False,
        ),
        "outs": None,
        "srcs": None,
        "cmd": None,
        "tools": None,
        "message": None,
    },
    doc = "Generates a SOME/IP Gateway configuration binary based on a specified json file.",
)

def _validate_someip_config_impl(name, json, expected_failure, **kwargs):
    """Validates a SOME/IP gateway config json against its corresponding schema."""

    validate_json_schema_test(
        name = name,
        json = json,
        schema = "@score_someip_gateway//src/gatewayd:etc/gatewayd_config_schema.json",
        expected_failure = expected_failure,
        **kwargs
    )

validate_someip_config_test = macro(
    doc = "Validates a SOME/IP gateway config json against its corresponding schema.",
    implementation = _validate_someip_config_impl,
    inherit_attrs = validate_json_schema_test,  # type: ignore
    attrs = {
        "json": attr.string(
            doc = "The input JSON configuration file.",
            mandatory = True,
        ),
        "expected_failure": attr.bool(default = False),
        "schema": None,
    },
)
