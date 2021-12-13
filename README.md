# autocmd-pattern-parser

A work-in-progress autocmd pattern parser, for converting legacy filetype detection
autocommands to lua.

## Build and run

    make
    ./auparser /usr/share/nvim/runtime/filetype.vim

### Options

* `-u` to unroll branches
* `-t` to exclude tree from output
* `-p` to parse raw patterns (one pattern per line)
* `-` for stdin

## Output

```json
[
  {
    "pattern": "...",       // original pattern
    "lnum": 1,              // source line number
    "cmd": "...",           // autocmd command, eg. "setf json"
    "tree": [               // tree
      [                     // branch, as in "abc" in "abc,def"
        {                   // token
          "type": "...",    // token type
          "value": "..."    // raw string
        },
        {
          "type": "Branch", // nested branch
          "value": [        // alternatives, same format as in branch ie. arrays of tokens
            [               // alternative
              {"type": "...", "value": "..."},
              {"type": "...", "value": "..."}
            ],
            []              // empty alternative, as in "a{b,}c"
          ]
        }
      ]
    ],
    "result": [             // results from branch unrolling
      {
        "pattern": "...",   // raw pattern for this branch
        "tokens": [         // array of tokens
          {"type": "...", "value": "..."},
          {"type": "...", "value": "..."}
        ]
      }
    ]
  },
  {
    "pattern": "...",
    "lnum": 1,
    "cmd": "...",
    "error": "..." // optional error, although it parses nvim runtime and polyglot just fine
  }
]
```

### Token types

Defined as `type_t` in [auparser.h](auparser.h).

| `Literal`    | matches string literals                                        |
| `AnyChar`    | matches `?` (translates to `.` in vim/lua regex)               |
| `AnyChars`   | matches `*` (translates to `.*` in vim/lua regex)              |
| `Set`        | matches character sets, eg. `[^2-3abc]`                        |
| `Cls`        | matches character class, eg. `\d` `\s` `\X`                    |
| `Opts`       | matches vim regex settings, eg. `\c` for turning on ignorecase |
| `ZeroOrMore` | matches `\*`                                                   |
| `ZeroOrOne`  | matches `\=` (basically `\?`)                                  |
| `OneOrMore`  | matches `\+`                                                   |
| `Count`      | previous atom repeated N times, eg. `\\\{6\}`                  |
