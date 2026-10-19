"""Curated mapping from Stack Overflow tags to technology domains (categories) and the
primary-technology tags used as "branches". Order is display order and therefore part of the data
contract (sg::graph::Profile stores the 0-based index)."""

CATEGORIES = [
    ("web", "Web Development", "Browsers, front-end and server-side web frameworks"),
    ("dotnet", ".NET & Windows", "C#, VB.NET, the .NET libraries and Windows desktop development"),
    ("jvm", "Java & JVM", "Java, its frameworks, tools and application servers"),
    ("systems", "Systems Programming", "C, C++, operating systems, concurrency and native code"),
    ("scripting", "Scripting Languages", "Python, Ruby, Perl, shells and text processing"),
    ("data", "Databases & Data", "SQL, relational databases, XML and spreadsheets"),
    ("mobile", "Mobile & Apple", "iPhone, Cocoa, Objective-C and macOS"),
    ("tooling", "DevOps & Tooling", "Version control, testing, debugging, builds and deployment"),
    ("cs", "Computer Science", "Algorithms, design, performance and language-agnostic questions"),
    ("security", "Security & Networking", "Security, cryptography, authentication and networking"),
]

# Branch tags per category, most popular first. Every branch is also a tag mapped to its category.
BRANCHES = {
    "web": ["javascript", "php", "asp.net", "html", "jquery", "css", "asp.net-mvc", "ruby-on-rails",
            "django", "ajax", "apache-flex", "flash"],
    "dotnet": ["c#", ".net", "vb.net", "winforms", "wpf", "linq", "wcf", "sharepoint", "visual-studio",
               "winapi", "powershell"],
    "jvm": ["java", "eclipse", "swing", "hibernate", "spring", "jakarta-ee"],
    "systems": ["c++", "c", "linux", "multithreading", "delphi", "qt", "opengl", "unix"],
    "scripting": ["python", "ruby", "perl", "bash", "regex"],
    "data": ["sql", "sql-server", "mysql", "database", "oracle", "postgresql", "sqlite", "xml", "excel"],
    "mobile": ["iphone", "objective-c", "cocoa", "macos"],
    "tooling": ["svn", "git", "unit-testing", "debugging", "version-control", "deployment"],
    "cs": ["algorithm", "language-agnostic", "performance", "design-patterns", "oop",
           "functional-programming", "haskell"],
    "security": ["security", "encryption", "authentication", "networking", "ssl"],
}

# Additional (non-branch) tags that still indicate a domain.
EXTRA_TAGS = {
    "web": ["internet-explorer", "firefox", "browser", "dom", "http", "jsp", "json", "rest", "web-services",
            "iis", "apache", "asp-classic", "xhtml", "actionscript-3", "actionscript", "silverlight",
            "web-applications", "asp.net-ajax", "iis-6", "forms", "session", "url"],
    "dotnet": ["visual-studio-2008", "visual-studio-2005", "linq-to-sql", "xaml", "ado.net", "nhibernate",
               "entity-framework", "windows", "com", "mfc", "vb6", "visual-c++", "msbuild", "tfs",
               "windows-mobile", ".net-3.5", ".net-2.0", "data-binding", "gridview", "user-controls",
               "windows-services", "interop", "compact-framework", "windows-installer", "vba", "vbscript"],
    "jvm": ["tomcat", "maven-2", "jsp", "generics"],
    "systems": ["memory", "stl", "gcc", "embedded", "64-bit", "concurrency", "dll", "memory-management",
                "file-io", "process", "windows-xp", "windows-vista", "filesystems", "templates"],
    "scripting": ["shell", "scripting", "batch-file", "command-line", "emacs", "vim"],
    "data": ["t-sql", "sql-server-2005", "stored-procedures", "database-design", "ms-access", "xslt",
             "orm", "indexing", "transactions", "reporting-services", "xsd", "serialization"],
    "mobile": ["cocoa-touch"],
    "tooling": ["testing", "tdd", "deployment", "installation", "build-process", "build-automation", "ide",
                "configuration", "logging", "refactoring", "open-source", "project-management"],
    "cs": ["data-structures", "math", "compiler-construction", "sorting", "architecture", "optimization",
           "language-agnostic", "coding-style", "lisp", "parsing", "recursion", "theory"],
    "security": ["active-directory", "hash", "sockets", "email", "cryptography", "passwords"],
}

FALLBACK = ("cs", "language-agnostic")


def tag_category() -> dict[str, str]:
    mapping: dict[str, str] = {}
    for slug, tags in BRANCHES.items():
        for tag in tags:
            mapping[tag] = slug
    for slug, tags in EXTRA_TAGS.items():
        for tag in tags:
            mapping.setdefault(tag, slug)
    return mapping
