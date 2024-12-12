syntax keyword tinyTodos TODO FIXME NOTE

syntax keyword tinyKeywords
            \ if
            \ else
            \ null
            \ while
            \ for
            \ func
            \ return
            \ foreign
            \ new
            \ cast
            \ break
            \ continue
            \ use
            \ as
            \ struct
            \ foreach
            \ in
            \ in_reverse

syntax match tinyNumber "\v<\d+>"
syntax match tinyNumber "\v<\d+\.\d+>"
syntax match tinyNumber "\v<0x\x+>"

syntax match tinyBoolean "\v<true|false>"

syntax match tinyTypeIdent "\v[a-zA-Z0-9_]+" contained

syntax region tinyTypeContainer start=/\v:\s+/ end=/\v$|,/ contains=tinyTypeIdent oneline

syntax region tinyString start=/"/ skip=/\\"/ end=/"/ oneline

syntax match tinyOperator "\V<+\|-\|*\|/\|%\|=\|:=\|::\|+=\|-=\|*=\|/=\|%=>"

syntax region tinyComment start="//" end="$" oneline

syntax region tinyChar start=+'+ end=+'+ oneline

highlight default link tinyTodos Todo
highlight default link tinyKeywords Keyword
highlight default link tinyNumber Number
highlight default link tinyString String
highlight default link tinyBoolean Boolean
highlight default link tinyOperator Operator
highlight default link tinyTypeIdent Type
highlight default link tinyComment Comment
highlight default link tinyChar Character
