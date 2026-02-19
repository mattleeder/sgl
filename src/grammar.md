
**‘xxx’**: quoted boldface is used for tokens that appear verbatim (‘‘terminals’’);
xxx: regular typeface is used for names of language constructs (‘‘non-terminals’’);
( ): parentheses are used for grouping of language constructs;
x|y: indicates that either x or y can appear;
x?: indicates that x appears 0 or 1 times;
x*: indicates that x appears 0 or more times.

keywords are case insensitive.

| Lexical Elements |  |
|---|---|
|keyword            |<ul><li>**'SELECT'**</li><li>**'FROM'**</li><li>**'WHERE'**</li><li>**'AND'**</li></ul>|
|symbol             | <ul><li>**';'**</li><li>**','**</li><li>**'='**</li><li>**'!'**</li><li>**'<'**</li><li>**'>'**</li><li>**'*'**</li></ul>|
|integerConstant    ||
|stringConstant     ||
|identifier         ||


| Statement Structure |  |
|---|---|
| selectStatment    | **'SELECT'** expressionList fromStatement |
| fromStatement     | **'FROM'** tableName whereStatement?|
| whereStatement    | **'WHERE'** expression |
| columnName        | identifier |
| tableName         | identifier |
| functionName      | idenfifier |


| Expressions |  |
|---|---|
| expression        | term (op term)* |
| term              | integerConstant \| stringConstant \| columnName \| functionCall \| star |
| op                | **'='** \| **'<'** \| **'>'** \| **'!='** |
| functionCall      | functionName **'('** expressionList **')'** |
| expressionList    | (expression (**','** expression)* )? |
| star              | **'*'** |