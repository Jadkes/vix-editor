import json
import random
import difflib
import os
import re

# Path to the database
DB_PATH = 'vix_data.json'

class JarvisBrain:
    def __init__(self):
        self.intents = []
        # Core C++ Language
        self.cpp_keywords = [
            "int", "main", "return", "include", "iostream", "std", "cout", "cin", "endl", 
            "vector", "string", "class", "public", "private", "protected", "void", "using", "namespace",
            "if", "else", "for", "while", "do", "switch", "case", "break", "continue",
            "struct", "template", "typename", "virtual", "override", "new", "delete",
            "unsigned", "long", "char", "float", "double", "bool", "true", "false",
            "printf", "scanf", "malloc", "free", "size_t", "nullptr", "static", "const"
        ]
        self.cpp_headers = ["iostream", "vector", "string", "fstream", "algorithm", "chrono", "thread", "mutex", "map", "set", "cmath", "cstdio", "cstdlib"]
        self.load_intents()

    def load_intents(self):
        if not os.path.exists(DB_PATH): return
        try:
            with open(DB_PATH, 'r', encoding='utf-8') as f:
                data = json.load(f)
                self.intents = data.get('intents', [])
        except: pass

    def get_ghost_suggestion(self, current_line, buffer_text):
        """Ultra-Pro Suggestion Engine"""
        clean_line = current_line.strip()
        if not current_line or (len(current_line) > 0 and current_line[-1] in " \t()[]{};,+-*/=<>!&|"):
            return ""

        # Find prefix (last word)
        match = re.search(r'(\w+)$', current_line)
        if not match: return ""
        prefix = match.group(1)
        if len(prefix) < 1: return ""

        # Contextual Symbol Learning (Ignore strings and comments)
        clean_buffer = re.sub(r'".*?"', '', buffer_text)
        clean_buffer = re.sub(r'//.*|/\*.*?\*/', '', clean_buffer, flags=re.DOTALL)
        
        # Priority 1: Check for #include
        if current_line.lstrip().startswith("#include"):
            h_match = re.search(r'#include\s*[<\"]([\w/.]*)$', current_line)
            if h_match:
                h_prefix = h_match.group(1)
                opts = [h for h in self.cpp_headers if h.startswith(h_prefix)]
                if opts: return opts[0][len(h_prefix):]

        # Priority 2: Extract symbols from local context (last 20 lines)
        local_lines = buffer_text.split('\n')
        # symbols = set(re.findall(r'\b\w+\b', clean_buffer))
        symbols = set(re.findall(r'\b\w+\b', clean_buffer))
        symbols.update(self.cpp_keywords)

        options = [s for s in symbols if s.startswith(prefix) and s != prefix]
        if not options: return ""

        # Sorting: Keywords first, then alphabetical
        options.sort(key=lambda x: (0 if x in self.cpp_keywords else 1, len(x), x))
        
        return options[0][len(prefix):]

    def lint_cpp(self, buffer_text):
        """Real-time C++ Linter"""
        errors = []
        lines = buffer_text.split('\n')
        brace_stack = []
        for i, line in enumerate(lines):
            clean = line.strip()
            if not clean or clean.startswith("//") or clean.startswith("#"): continue
            
            if "count <<" in clean: errors.append({"line": i, "msg": "Typo: Did you mean 'cout'?"})
            
            if clean and not clean.endswith('{') and not clean.endswith('}') and \
               not clean.endswith(':') and not clean.endswith(';') and \
               not clean.startswith("if") and not clean.startswith("for") and \
               not clean.startswith("while") and "main(" not in clean:
                errors.append({"line": i, "msg": "Missing semicolon?"})

            for c in line:
                if c == '{': brace_stack.append(i)
                elif c == '}':
                    if brace_stack: brace_stack.pop()
                    else: errors.append({"line": i, "msg": "Unexpected '}'"})
        for l in brace_stack: errors.append({"line": l, "msg": "Unclosed '{'"})
        return errors

brain = JarvisBrain()