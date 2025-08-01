import itertools
import os
import re
import shutil
import subprocess
from pathlib import Path

import pyparsing as pp
import yaml

# -----------------------------------------------------------------------------------------
# Module information parsing and generation
# -----------------------------------------------------------------------------------------

# simple parser for vtk.module files
LINE = pp.ungroup(
    ~pp.StringEnd() + pp.restOfLine
)  # check first for empty line to avoid being stuck in a tag empty IndentedBlock
# A key-value section of a module file is a key at the start of the line which is always capital and optionally an indented block that holds the value.
MODULE_FILE_PARSER = pp.Group(
    pp.LineStart()
    + pp.Word(pp.alphas.upper() + "_")
    + pp.Optional(pp.IndentedBlock(LINE), [])
)
MODULE_FILE_PARSER.ignore("#" + LINE)


def parse_vtk_module(filepath):
    """Create a dictionary that holds the structure of the given vtk.module
    located at filepath. The return value is a dictionary(key,list(lines)) where key is
    the module keywords and the list(lines) the value corresponding to this key
    as a list of lines.
    """
    with open(filepath, "r") as f:
        data = MODULE_FILE_PARSER[...].parseString(f.read())
        structure = {}
        for group in data:
            structure[str(group[0])] = list(group[1])
    return structure


def gather_module_documentation(
    basepath,
    root_destination,
    custom_paths=[],
    readme_formats=["README.md", "README", "readme.md", "README.txt"],
    extra_documentation_dirs=[],
    ignore_list=[],
):
    """For every module directory under basepath (i.e. contains a vtk.module file), copy READMEs under root_destination
    while recreating the directory structure. Additionally look for README under custom_paths.

    A "README" file is any file matching the readme_formats.
    returns a list of dictionaries holding the description of a module (see also parse_vtk_module)

    extra_documentation_dirs: directories to look for additional documentation
    ignore_list: paths relative to basepath to ignore
    """
    try:
        os.mkdir(root_destination)
    except FileExistsError:
        pass
    paths = Path(basepath).rglob("vtk.module")
    if len(custom_paths) > 0:
        custom_dirs = iter([Path(path) for path in custom_paths])
        paths = itertools.chain(paths, custom_dirs)

    # transform the ignore_list to Path objects
    ignore_list = [Path(os.path.join(basepath, path)) for path in ignore_list]

    module_list = []

    for path in paths:
        basename = path
        if path.is_file():
            basename = path.parent

        skip = False
        for item in ignore_list:
            if basename.is_relative_to(item):
                skip = True
                break
        if skip:
            continue

        # extract module information
        if "vtk.module" in str(path):
            module = parse_vtk_module(str(path))
            for doc_dir_name in extra_documentation_dirs:
                doc_dir = os.path.join(str(basename), doc_dir_name)
                if os.path.exists(doc_dir):
                    new_doc_dir = os.path.relpath(doc_dir, start="../../")
                    dest = Path(os.path.join(root_destination, new_doc_dir))
                    shutil.copytree(
                        doc_dir,
                        dest,
                        dirs_exist_ok=True,
                    )
            # Bring module specific readmes while recreating the data structure
            for readme in readme_formats:
                readme = os.path.join(basename, readme)
                if os.path.exists(readme):
                    new_readme = os.path.relpath(readme, start="../../")
                    dest = Path(os.path.join(root_destination, new_readme))

                    # make sure it is a markdown file
                    if not dest.suffix == ".md":
                        dest = dest.with_suffix(".md")
                    destdir = os.path.dirname(dest)
                    if not os.path.exists(destdir):
                        os.makedirs(destdir)
                    shutil.copy(readme, dest)
                    module["readme"] = dest
            module_list.append(module)
    return module_list


def create_module_table(module_list):
    """Generate a markdown table holding name and description of each vtk
    module"""

    table = "| Module Name | Description|\n"
    table += "|-------------|------------|\n"
    for module in sorted(module_list, key=lambda item: item["NAME"][0]):
        name = "{{bdg-primary-line}}`{module_name}`".format(
            module_name=module["NAME"][0]
        )
        description = module.get("DESCRIPTION", [""])[0]
        # extra documentation  exists add link
        if "readme" in module.keys():
            value = module["readme"]
            description += f" [{{material-regular}}`menu_book;2em`](../{value})"
        line = "| " + name + "|" + description + "|"
        table += line + "\n"
    return table


# -----------------------------------------------------------------------------------------
# Helpers for autodocs2
# -----------------------------------------------------------------------------------------


def add_init_file(path):
    """By default the  Wrapping/Python/vtkmodules/__init__.py.in has contains
    no modules since it is populated during configuration time.  However,
    autodoc2 needs an initialized __init__.py to generate documentation. We use
    a local copy which is extracted from vtk-9.2.6-cp311-cp311-win_amd64.whl.
    @todo we need to make this automatic.
    """
    shutil.copy("vtkmodules.__init__.py", path)


# -----------------------------------------------------------------------------------------
# Manual substitutions
#
# For cases where relative-docs is not enough
# -----------------------------------------------------------------------------------------

MANUAL_SUBSTITUTIONS = [
    {
        "source": "../../ThirdParty/imported.md",
        "destination": "./developers_guide/git/thirdparty-projects.md",
        "substitutions": [
            (
                r"\[.+\]\(UPDATING.md\)",
                "[](thirdparty.md)",
            ),
            (
                r"\* \[(\w+)\]\((.+\/update\.sh)\)$",
                "* \\1",
            ),
        ],
    },
    {
        "source": "../../ThirdParty/UPDATING.md",
        "destination": "./developers_guide/git/thirdparty.md",
        "substitutions": [
            (
                r"\[imported.md\]\(imported.md\)",
                "[](thirdparty-projects.md)",
            ),
            (
                r"\[update-common.sh\]\(update-common.sh\)",
                "[update-common.sh](path:../../../../ThirdParty/update-common.sh)",
            ),
        ],
    },
]


def copy_with_substitutions(source, destination, substitutions):
    """Copy "source" to "destination" while applying the replacements
    described substitutions. This is useful in case source  has links that
    cannot be updated with just a `relative-docs` since its position in the new
    documentation tree as well as the position of the dependents changed. So we
    copy the file and replace the links explicitly."""
    with open(source, "r") as f:
        content = f.read()
        for before, after in substitutions:
            content = re.sub(before, after, content, flags=re.M)
    with open(destination, "w") as f:
        f.write(content)


# -----------------------------------------------------------------------------------------
# Release notes Generation
# -----------------------------------------------------------------------------------------

HISTORICAL_RELEASE_URLS = {
    # could not find notes for older releases
    "v5.0.0": "https://www.kitware.com/vtk-5-0-released",
    "v5.2.0": "https://www.kitware.com/vtk-5-2-released",
    "v5.4.0": "https://www.kitware.com/vtk-5-4-released",
    "v5.6.0": "https://itk.org/Wiki/VTK_5.6_Release_Planning",
    "v5.8.0": "https://www.kitware.com/vtk-5-8-0",
    "v5.10.0": "https://www.kitware.com/vtk-5-10-now-available",
    "v6.0.0": "https://www.kitware.com/vtk-6-0-0",
    "v6.1.0": "https://www.kitware.com/vtk-6-1-0",
    "v6.2.0": "https://www.kitware.com/vtk-6-2-0",
    "v6.3.0": "https://www.kitware.com/vtk-6-3-0",
    "v7.0.0": "https://www.kitware.com/vtk-7-0-0",
    "v7.1.0": "https://www.kitware.com/vtk-7-1-0",
    "v8.0.0": "https://www.kitware.com/vtk-8-0-0",
    "v8.1.0": "https://www.kitware.com/vtk-8-1-0",
    "v8.2.0": "https://www.kitware.com/vtk-8-2-0",
}


def create_release_file(path, content):
    with open(path, "w") as file:
        file.write(content)


# This is a template for historical versions which we do not have a release
# document in the repository
CONTENT_TEMPLATE = """\
# {version}

Released on {date}.

Release notes for version {version} can be found at {url}.\n
"""


# This is a template for releases that we have a md file in the repository.
# Notice that we enable myst_all_links_external in the preamble. This is to avoid warnings
# generated by the links with no text in the document.
CONTENT_TEMPLATE2 = """\
# {version}

Released on {date}.

```{{include}} ../../release/_{version}-stripped.md
:relative-images:
:heading-offset: 1
```

```{{toctree}}
:hidden:

{author_notes}
```
"""


def strip_sphinx_exclude_blocks_from_file(input_path):
    """
    Read a Markdown file, remove blocks between
    <!-- sphinx-exclude-start --> and <!-- sphinx-exclude-end -->,
    and return the cleaned content as a string.
    """
    with open(input_path, "r") as input_file:
        content = input_file.read()
    return re.sub(
        r"<!--\s*sphinx-exclude-start\s*-->.*?<!--\s*sphinx-exclude-end\s*-->",
        "",
        content,
        flags=re.DOTALL,
    )


def write_stripped_release_file(input_path, output_path):
    """
    Strip sphinx-exclude block from input_path and write the result to output_path.
    """
    content = strip_sphinx_exclude_blocks_from_file(input_path)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w") as output_file:
        output_file.write(content)


def extract_image_links(content):
    """Extract image links of the form ![](../imgs/X.Y/filename.png) from markdown content.
    """
    return re.findall(r'!\[.*?\]\(\.\./imgs/([\d.]+)/([^)]+)\)', content)


def create_release_index(basedir):
    """Populate basedir with release files X.Y.md with X,Y being the major and minor versions respectively.
    it returns a string holding the toctree of the index file to be injected in basedir/index.md
    """
    # get x.y.0 releases skipping rc's
    command = r"git tag --sort=version:refname  --format '%(refname:strip=2) %(creatordate:format:%Y-%m-%d)'  | grep -v 'rc' | grep 'v[0-9]*\.[0-9]*\.0'"
    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    tags = str(result.stdout).split("\n")[:-1]  # drop last empty line
    tags = tags[4:]  # skip release for which we do not have release notes
    files = []
    for entry in tags:
        tag, date = entry.split(" ")
        short_tag = tag[1:]  # strip 'v'
        short_tag = short_tag.rsplit(".", 1)[0]  #  drop last '.0'
        if tag in HISTORICAL_RELEASE_URLS:
            content = CONTENT_TEMPLATE.format(
                version=short_tag, url=HISTORICAL_RELEASE_URLS[tag], date=date
            )
            files.append(f"{short_tag}.md")
            create_release_file(
                path=os.path.join(basedir, f"{short_tag}.md"), content=content
            )
        else:  # look for release note markdown
            release_file_path = f"../release/{short_tag}.md"
            if os.path.exists(release_file_path):
                stripped_release_path = os.path.join("../release", f"_{short_tag}-stripped.md")
                write_stripped_release_file(release_file_path, stripped_release_path)
                with open(stripped_release_path, "r") as release_file:
                    release_file_content = release_file.read()
                author_notes = []
                short_tag_dir = f"../release/{short_tag}"
                if os.path.exists(short_tag_dir):
                    for (dirpath, dirname, filenames) in os.walk(short_tag_dir):
                        dest_short_tag_dir = f"{basedir}/{short_tag}/"
                        if not os.path.exists(dest_short_tag_dir):
                            os.makedirs(dest_short_tag_dir)
                        for fn in filenames:
                            # Copy the note file if is referenced from included "{short_tag}.md"
                            full_fn = f"{short_tag}/{fn}"
                            if full_fn in release_file_content:
                                src = os.path.join("../release", full_fn)
                                dst = os.path.join(dest_short_tag_dir, fn)
                                shutil.copy(src, dst)
                                author_notes.append(full_fn.removesuffix(".md"))
                                with open(src, "r") as note_file:
                                    note_content = note_file.read()
                                for img_version, img_file in extract_image_links(note_content):
                                    src_img = os.path.join("../release/imgs", img_version, img_file)
                                    dst_img_dir = os.path.join("release_details/imgs", img_version)
                                    os.makedirs(dst_img_dir, exist_ok=True)
                                    shutil.copy(src_img, os.path.join(dst_img_dir, img_file))
                            else:
                                print(f"Warning: '{short_tag}/{fn}' is not referenced from '{release_file_path}'")
                content = CONTENT_TEMPLATE2.format(version=short_tag, date=date, author_notes="\n".join(sorted(author_notes)).strip())
                create_release_file(
                    path=os.path.join(basedir, f"{short_tag}.md"), content=content
                )
                files.append(f"{short_tag}.md")
            else:
                print(f"Warning: could not find release notes for tag {tag}")
    # now create the base index file. We do it here to control the order they appear in the index
    # we could do it using a custom sphinx toctree class but I didn't had much luck with that
    content = """\
```{toctree}
:titlesonly:
:caption: Release Notes

"""

    for file in reversed(files):
        content += f"{file}\n"
    content += "```\n"  # close {toctree}
    return content


# -----------------------------------------------------------------------------------------
# Supported Data formats Generation
# -----------------------------------------------------------------------------------------


def create_supported_formats_list(yaml_file):
    """Create a markdown snippet to be injected in supported_data_formats.md
    yaml_file should point to the yaml file that holds the relevant information"""

    output = ""
    try:
        with open(yaml_file, "r") as file:
            database = yaml.safe_load(file)
            for entry in sorted(
                database, key=lambda item: item["extensions"][0]
            ):  # sort by first extension
                extensions = ", ".join(
                    entry["extensions"]
                )  # remove brackets from list representation
                plural = ""
                if len(entry["extensions"]) > 1:
                    plural = "s"
                output += f"""
* {entry['description']}:
    - Extension{plural}: {extensions}
"""
                if entry["reader_class"]:
                    output += f"""
    - reader: [{entry['reader_class']}](https://vtk.org/doc/nightly/html/class{entry['reader_class']}.html)
"""
                if entry["writer_class"]:
                    output += f"""
    - writer: [{entry['writer_class']}](https://vtk.org/doc/nightly/html/class{entry['writer_class']}.html)
"""
                output += f"""
    - module: {{bdg-primary-line}}`{entry['vtk_module']}`
"""
    except:
        # make sure something renders so the user knowns if there is a problem.
        output = f"""\
```{{warning}}
Error parsing  '{yaml_file}'
```
"""
    return output
