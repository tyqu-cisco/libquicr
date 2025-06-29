"""Code Generator"""

from typing import List, Dict, Set
from importlib import resources
from jinja2 import Environment, FileSystemLoader, PackageLoader
from .message_spec import MessageSpec


class CodeGenerator:
    """MessageSpec Code Generator"""

    def __init__(self, language: str, source_filename: str):
        self.package_name = "draft_parser"
        self.template_language = language
        self.source_filename = source_filename
        self.package_path = resources.files(__package__)
        self.template_path = (
            f"{self.package_path}/templates/{self.template_language}"
        )
        self.template_env = Environment(
            loader=PackageLoader(
                "moqt_parser", package_path="./templates/cpp"
            ),
            trim_blocks=True,
            lstrip_blocks=True,
        )

    def generate_header_message_begin(self) -> str:
        """Gen Header Beginning"""
        gen_begin = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_Begin_tmpl.jinja2"
        )
        gen_begin += template.render(
            source_filename=self.source_filename
        )
        return gen_begin

    def generate_header_message_end(self, using_map) -> str:
        """Gen Header End"""
        gen_end = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_End_tmpl.jinja2"
        )
        gen_end += template.render(using_map=using_map)
        return gen_end

    def generate_header_message_using(
        self, using_map: Dict, repeat_using_map: Dict
    ) -> str:
        """Gen Header Using"""
        gen_usings = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_Using_tmpl.jinja2"
        )
        gen_usings += template.render(
            using_map=using_map, repeat_map=repeat_using_map
        )
        return gen_usings

    def generate_header_message_enums(
        self, messages: List[MessageSpec]
    ) -> str:
        """Gen Header Enums"""
        gen_enums = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_Enums_tmpl.jinja2"
        )
        messages.sort(key=lambda message: message.message_enum, reverse=False)
        gen_enums += template.render(messages=messages)
        return gen_enums

    def generate_header_message_structs(
        self, messages: List[MessageSpec], field_discards: List[str]
    ) -> str:
        """Gen Header Structs"""
        gen = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_Structs_tmpl.jinja2"
        )
        for message in messages:
            gen += template.render(
                message=message, field_discards=field_discards
            )
        return gen

    def generate_source_message_begin(
        self, _message: List[MessageSpec]
    ) -> str:
        """Gen Source Beginning"""
        gen_begin = ""
        template = self.template_env.get_template(
            "MessageSpec_Source_Begin_tmpl.jinja2"
        )
        gen_begin += template.render(
            source_filename=self.source_filename
        )
        return gen_begin

    def generate_source_message_encode_decode(
        self, messages: List[MessageSpec], field_discards: List[str]
    ) -> str:
        """Gen Source Encode/Decode Methods"""
        gen = ""
        template = self.template_env.get_template(
            "MessageSpec_Source_Structs_tmple.jinja2"
        )
        for message in messages:
            gen += template.render(
                message=message, field_discards=field_discards
            )
        return gen

    def generate_source_message_using(
        self, using_map: Dict, repeat_using_map: Dict
    ) -> str:
        """Gen Source Using"""
        gen_usings = ""
        template = self.template_env.get_template(
            "MessageSpec_Source_Using_templ.jinja2"
        )
        gen_usings += template.render(
            using_map=using_map, repeat_map=repeat_using_map
        )
        return gen_usings

    def generate_source_message_end(self, using_map: Dict) -> str:
        """Gen Source End"""
        gen_end = ""
        template = self.template_env.get_template(
            "MessageSpec_Source_End_tmpl.jinja2"
        )
        gen_end += template.render(using_map=using_map)
        return gen_end

    def generate_header(
        self,
        messages: List[MessageSpec],
        using_map: Dict,
        repeat_using_map: Dict,
        field_discards: List[str],
    ) -> str:
        """Generate Complete Header"""
        header = ""
        header += self.generate_header_message_begin()
        header += self.generate_header_message_using(
            using_map, repeat_using_map
        )
        header += self.generate_header_message_enums(messages)
        header += self.generate_header_message_structs(
            messages, field_discards
        )
        header += self.generate_header_message_end(using_map)
        return header

    def generate_source(
        self,
        messages: List[MessageSpec],
        using_map: Dict,
        repeat_using_map: Dict,
        field_discards: List[str],
    ) -> str:
        """Generate Complete Source"""
        source = ""
        source += self.generate_source_message_begin(messages)
        source += self.generate_source_message_using(
            using_map, repeat_using_map
        )
        source += self.generate_source_message_encode_decode(
            messages, field_discards
        )
        source += self.generate_source_message_end(using_map)
        return source

    def generate_tranport_header_message_begin(self) -> str:
        """Gen Header Beginning"""
        gen_begin = ""
        template = self.template_env.get_template(
            "Transport_Header_Begin_tmpl.jinja2"
        )
        gen_begin += template.render()
        return gen_begin

    def generate_tranport_header_message_end(self, using_map) -> str:
        """Gen Header End"""
        gen_end = ""
        template = self.template_env.get_template(
            "Transport_Header_End_tmpl.jinja2"
        )
        gen_end += template.render(using_map=using_map)
        return gen_end

    def generate_tranport_header_message_methods(
        self, messages: List[MessageSpec]
    ) -> str:
        """Gen Header Structs"""
        gen = ""
        template = self.template_env.get_template(
            "Transport_Header_Structs_tmpl.jinja2"
        )
        for message in messages:
            gen += template.render(message=message)
        return gen

    def generate_transport_source_begin_message_methods(
        self, _messages: List[MessageSpec]
    ) -> str:
        """Gen Transport Source Begin"""
        gen = ""
        template = self.template_env.get_template(
            "Transport_Source_Begin_tmpl.jinja2"
        )
        gen += template.render()
        return gen

    def generate_transport_source_message_methods(
        self, messages: List[MessageSpec]
    ) -> str:
        """Gen Source Structs"""
        gen = ""
        template = self.template_env.get_template(
            "Transport_Source_Structs_tmpl.jinja2"
        )
        for message in messages:
            gen += template.render(message=message)
        return gen

    def generate_transport_source_end_message_methods(
        self, _messages: List[MessageSpec]
    ) -> str:
        """Gen Source Structs"""
        gen = ""
        template = self.template_env.get_template(
            "Transport_Source_End_tmpl.jinja2"
        )
        gen += template.render()
        return gen

    def generate_transport_header(
        self, messages: List[MessageSpec], using_map: Dict
    ) -> str:
        """Generate Complete Header"""
        header = ""
        header += self.generate_tranport_header_message_begin()
        # header += self.generate_header_message_using(using_map)
        # header += self.generate_header_message_enums(messages)
        header += self.generate_tranport_header_message_methods(messages)
        header += self.generate_tranport_header_message_end(using_map)
        return header

    def generate_transport_source(
        self, messages: List[MessageSpec], using_map: Dict
    ) -> str:
        """Generate Complete Source"""
        source = ""
        source += self.generate_transport_source_begin_message_methods(
            messages
        )
        ## source += self.generate_source_message_using(using_map)
        source += self.generate_transport_source_message_methods(messages)
        source += self.generate_transport_source_end_message_methods(using_map)
        return source
