import json
import random
import difflib
import os
import re

# Path to the database
DB_PATH = 'vix_data.json'

class Jarvis_brain:
    def __init__(self):
        self.intents = []

        # Cached keyword sets for O(1) lookup
        self._cpp_keywords_set = None
        self._python_keywords_set = None
        self._js_keywords_set = None
        self._rust_keywords_set = None
        self._go_keywords_set = None

        # Suggestion cycling state
        self._last_prefix = ""
        self._last_options = []
        self._last_index = 0

        # Core C++ Language
        self.cpp_keywords = [
            "int", "main", "return", "include", "iostream", "std", "cout", "cin", "endl",
            "vector", "string", "class", "public", "private", "protected", "void", "using", "namespace",
            "if", "else", "for", "while", "do", "switch", "case", "break", "continue",
            "struct", "template", "typename", "virtual", "override", "new", "delete",
            "unsigned", "long", "char", "float", "double", "bool", "true", "false",
            "printf", "scanf", "malloc", "free", "size_t", "nullptr", "static", "const",
            "auto", "register", "extern", "sizeof", "typedef", "enum", "union", "operator",
            "friend", "explicit", "mutable", "this", "try", "catch", "throw", "noexcept",
            "constexpr", "decltype", "final", "override", "alignof", "alignas", "thread_local"
        ]
        self.cpp_headers = ["iostream", "vector", "string", "fstream", "algorithm", "chrono", "thread", "mutex", "map", "set", "cmath", "cstdio", "cstdlib", "array", "list", "deque", "queue", "stack", "unordered_map", "unordered_set", "memory", "functional", "optional", "variant", "any", "future", "promise"]

        # Python keywords
        self.python_keywords = [
            "def", "class", "if", "elif", "else", "for", "while", "try", "except", "finally",
            "with", "as", "import", "from", "return", "yield", "raise", "pass", "break", "continue",
            "and", "or", "not", "in", "is", "lambda", "True", "False", "None", "global", "nonlocal",
            "assert", "del", "async", "await", "self", "print", "len", "range", "enumerate", "zip",
            "map", "filter", "sorted", "reversed", "min", "max", "sum", "abs", "open", "input"
        ]
        self.python_headers = []

        # JavaScript keywords
        self.js_keywords = [
            "function", "const", "let", "var", "async", "await", "import", "export", "class", "extends",
            "constructor", "if", "else", "for", "while", "do", "switch", "case", "return", "try",
            "catch", "throw", "finally", "new", "delete", "typeof", "instanceof", "this", "super",
            "true", "false", "null", "undefined", "void", "break", "continue", "default", "yield",
            "static", "get", "set", "of", "in", "NaN", "Infinity", "console", "log", "document", "window"
        ]
        self.js_headers = []

        # Rust keywords
        self.rust_keywords = [
            "fn", "let", "mut", "const", "struct", "impl", "trait", "pub", "mod", "use", "crate", "self", "super",
            "if", "else", "for", "while", "loop", "match", "return", "async", "await", "move",
            "pub", "mod", "use", "as", "ref", "type", "where", "unsafe", "extern", "static",
            "true", "false", "Some", "None", "Ok", "Err", "Self", "self", "super", "crate",
            "break", "continue", "dyn", "enum", "false", "macro", "override", "priv", "true", "type"
        ]
        self.rust_headers = []

        # Go keywords
        self.go_keywords = [
            "func", "var", "const", "type", "struct", "interface", "package", "import",
            "if", "else", "for", "switch", "case", "return", "go", "defer", "chan", "select",
            "true", "false", "nil", "make", "new", "len", "cap", "append", "copy", "delete",
            "map", "range", "recover", "panic", "fallthrough", "goto", "break", "continue",
            "uint", "int", "float", "string", "bool", "byte", "rune", "error", "true", "false"
        ]
        self.go_headers = []

        self.load_intents()

    def detect_language(self, buffer_text, filename=""):
        """Detect language from buffer content and filename"""
        # Check file extension
        if filename:
            ext = filename.split('.')[-1].lower()
            if ext in ['js', 'jsx', 'mjs', 'cjs']:
                return 'javascript'
            elif ext in ['rs']:
                return 'rust'
            elif ext in ['go']:
                return 'go'
            elif ext in ['py', 'pyw']:
                return 'python'
            elif ext in ['cpp', 'cc', 'cxx', 'c', 'hpp', 'h', 'hxx']:
                return 'cpp'

        # Check for language indicators in buffer
        buffer_lower = buffer_text.lower()

        # Rust indicators
        if 'fn main' in buffer_text or 'fn ' in buffer_text or 'let mut' in buffer_text or 'impl ' in buffer_text or 'pub fn' in buffer_text or '::new()' in buffer_text:
            return 'rust'

        # Go indicators
        if 'package main' in buffer_text or 'func main()' in buffer_text or 'import (' in buffer_text or 'package ' in buffer_text.split('\n')[0]:
            return 'go'

        # JavaScript indicators
        if 'function ' in buffer_text or 'const ' in buffer_text or 'let ' in buffer_text or '=>' in buffer_text or 'require(' in buffer_text or 'export ' in buffer_text:
            return 'javascript'

        # Python indicators
        if 'def ' in buffer_text or 'import ' in buffer_text or 'class ' in buffer_text or 'if __name__' in buffer_text or 'print(' in buffer_text:
            return 'python'

        # C++ indicators
        if '#include' in buffer_text or 'std::' in buffer_text or 'cout' in buffer_text or 'cin' in buffer_text or 'endl' in buffer_text:
            return 'cpp'

        return 'cpp'  # Default to C++

    def load_intents(self):
        if not os.path.exists(DB_PATH): return
        try:
            with open(DB_PATH, 'r', encoding='utf-8') as f:
                data = json.load(f)
                self.intents = data.get('intents', [])
        except: pass

    def _get_cached_keywords(self, lang):
        if lang == 'python':
            if self._python_keywords_set is None:
                self._python_keywords_set = set(self.python_keywords)
            return self._python_keywords_set
        elif lang == 'javascript':
            if self._js_keywords_set is None:
                self._js_keywords_set = set(self.js_keywords)
            return self._js_keywords_set
        elif lang == 'rust':
            if self._rust_keywords_set is None:
                self._rust_keywords_set = set(self.rust_keywords)
            return self._rust_keywords_set
        elif lang == 'go':
            if self._go_keywords_set is None:
                self._go_keywords_set = set(self.go_keywords)
            return self._go_keywords_set
        else:
            if self._cpp_keywords_set is None:
                self._cpp_keywords_set = set(self.cpp_keywords)
            return self._cpp_keywords_set

    def _get_context_info(self, buffer_text, current_line_num):
        lines = buffer_text.split('\n')
        start = max(0, current_line_num - 20)
        recent = '\n'.join(lines[start:current_line_num])

        context = {
            'in_class': None,
            'in_function': None,
            'open_braces': 0,
            'open_parens': 0,
            'open_brackets': 0,
        }

        class_match = re.search(r'class\s+(\w+)', recent)
        if class_match:
            context['in_class'] = class_match.group(1)

        fn_match = re.search(r'(?:fn|func|def|void|int|char|long|float|double|auto)\s+(\w+)\s*\(', recent)
        if fn_match:
            context['in_function'] = fn_match.group(1)

        context['open_braces'] = recent.count('{') - recent.count('}')
        context['open_parens'] = recent.count('(') - recent.count(')')
        context['open_brackets'] = recent.count('[') - recent.count(']')

        return context

    def _get_context_suggestion(self, current_line, prev_line, context, prefix):
        stripped = current_line.strip()
        prev_stripped = prev_line.strip() if prev_line else ""

        if context['open_braces'] > 0 and not stripped:
            return "}"

        if context['open_parens'] > 0 and not stripped:
            return ")"

        if context['open_brackets'] > 0 and not stripped:
            return "]"

        if prev_stripped.endswith('{') and not stripped:
            return "    "

        if prev_stripped.endswith('(') and not stripped:
            return "    "

        if context['in_class'] and not stripped:
            method_patterns = ["def ", "void ", "int ", "public ", "private "]
            for p in method_patterns:
                if p.startswith(prefix) and len(prefix) > 0:
                    return p[len(prefix):]
            return "def "

        if context['in_function'] and prev_stripped.endswith('{') and not stripped:
            return "    "

        return None

    def _get_keyword_frequency(self, buffer_text, keywords_set):
        words = re.findall(r'\b\w+\b', buffer_text)
        freq = {}
        for w in words:
            if w in keywords_set:
                freq[w] = freq.get(w, 0) + 1
        return freq

    def get_ghost_suggestion(self, current_line, buffer_text, filename="", cycle=False):
        clean_line = current_line.strip()
        if not current_line or (len(current_line) > 0 and current_line[-1] in " \t()[]{};,+-*/=<>!&|"):
            self._last_prefix = ""
            self._last_options = []
            return ""

        match = re.search(r'(\w+)$', current_line)
        if not match:
            self._last_prefix = ""
            self._last_options = []
            return ""
        prefix = match.group(1)
        if len(prefix) < 1:
            self._last_prefix = ""
            self._last_options = []
            return ""

        lines = buffer_text.split('\n')
        current_line_num = len(lines) - 1 if current_line in [lines[-1], '\n'.join(lines[:-1])] else 0
        for i, l in enumerate(lines):
            if current_line in l or l.rstrip('\n') == current_line.rstrip('\n'):
                current_line_num = i
                break

        prev_line = lines[current_line_num - 1] if current_line_num > 0 else ""

        lang = self.detect_language(buffer_text, filename)
        keywords = self._get_cached_keywords(lang)
        headers = self.python_headers if lang == 'python' else (
            self.js_headers if lang == 'javascript' else (
                self.rust_headers if lang == 'rust' else (
                    self.go_headers if lang == 'go' else self.cpp_headers)))

        context = self._get_context_info(buffer_text, current_line_num)

        ctx_suggestion = self._get_context_suggestion(current_line, prev_line, context, prefix)
        if ctx_suggestion and ctx_suggestion.startswith(prefix):
            self._last_prefix = prefix
            self._last_options = []
            return ctx_suggestion

        if current_line.lstrip().startswith("#include"):
            h_match = re.search(r'#include\s*[<\"]([\w/.]*)$', current_line)
            if h_match:
                h_prefix = h_match.group(1)
                opts = [h for h in headers if h.startswith(h_prefix)]
                if opts:
                    self._last_prefix = prefix
                    self._last_options = opts
                    return opts[0][len(h_prefix):]

        start = max(0, current_line_num - 20)
        local_buffer = '\n'.join(lines[start:])
        clean_buffer = re.sub(r'".*?"', '', local_buffer)
        clean_buffer = re.sub(r'//.*|/\*.*?\*/', '', clean_buffer, flags=re.DOTALL)

        symbols = set(re.findall(r'\b\w+\b', clean_buffer))
        symbols.update(keywords)

        options = [s for s in symbols if s.startswith(prefix) and s != prefix]
        if not options:
            self._last_prefix = ""
            self._last_options = []
            return ""

        keyword_freq = self._get_keyword_frequency(clean_buffer, keywords)

        options.sort(key=lambda x: (
            0 if x in keywords else 1,
            -(keyword_freq.get(x, 0)),
            len(x),
            x
        ))

        if cycle and prefix == self._last_prefix and self._last_options:
            self._last_index = (self._last_index + 1) % len(options)
        else:
            self._last_index = 0

        self._last_prefix = prefix
        self._last_options = options

        result = options[self._last_index][len(prefix):]
        return result

    def lint_cpp(self, buffer_text):
        """Real-time C++ Linter"""
        errors = []
        lines = buffer_text.split('\n')
        brace_stack = []
        in_string = False
        string_char = None
        
        for i, line in enumerate(lines):
            clean = line.strip()
            if not clean or clean.startswith("//") or clean.startswith("#"): continue
            
            if "count <<" in clean and "cout" not in clean:
                errors.append({"line": i, "msg": "Typo: Did you mean 'cout'?"})
            
            if clean and not clean.endswith('{') and not clean.endswith('}') and \
               not clean.endswith(':') and not clean.endswith(';') and \
               not clean.startswith("if") and not clean.startswith("for") and \
               not clean.startswith("while") and not clean.startswith("switch") and \
               not clean.startswith("return") and "main(" not in clean:
                if '=' in clean or '(' in clean:
                    errors.append({"line": i, "msg": "Missing semicolon?"})

            for c in line:
                if c in '"\'' and not in_string:
                    in_string = True
                    string_char = c
                elif c == string_char and in_string:
                    in_string = False
                    string_char = None
                elif not in_string:
                    if c == '{': brace_stack.append(i)
                    elif c == '}':
                        if brace_stack: brace_stack.pop()
                        else: errors.append({"line": i, "msg": "Unexpected '}'"})
        for l in brace_stack: errors.append({"line": l, "msg": "Unclosed '{'"})
        return errors

brain = Jarvis_brain()