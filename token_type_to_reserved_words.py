import sys

TOKEN_TO_STRING = "0"
TOKEN_TO_RESERVED_WORDS = "1"

MODE_SET = {
    TOKEN_TO_STRING,
    TOKEN_TO_RESERVED_WORDS
}

def get_mode() -> str:
    usr_choice = input("Select mode. Type 0 for token_to_string, type 1 for token_to_reserved_words: ")
    usr_choice = usr_choice.strip()
    while (usr_choice not in MODE_SET):
        usr_choice = input(f"Did not understand '{usr_choice}'.\nSelect mode. Type 0 for token_to_string, type 1 for token_to_reserved_words: ")
        usr_choice = usr_choice.strip()
    return usr_choice

def get_reserved_words_input() -> str:
    print("Please paste keywords then press Ctrl-Z then Enter:\n")
    usr_input = sys.stdin.read()
    return usr_input

def get_token_to_string_input() -> str:
    print("Please paste tokens then press Ctrl-Z then Enter:\n")
    usr_input = sys.stdin.read()
    return usr_input

def clean_input(keywords: str) -> str:
    strip_characters = [" ", "\n", "\t"]
    for char in strip_characters:
        keywords = keywords.replace(char, "")
    return keywords

def input_to_list(keywords: str) -> list[str]:
    keywords = keywords.split(",")
    keywords = set(keywords)
    keywords = list(keywords)
    keywords.sort()
    return keywords

def print_reserved_words(keywords: list[str]):
    prefix_length = len("TOKEN_")
    max_len = max(len(word) for word in keywords) - prefix_length + 4
    names = [word[prefix_length:] for word in keywords]

    max_name = max(len(n) for n in names)
    max_token = max(len(word) for word in keywords)

    print("static const struct ReservedWord reserved_words[] = {", end = "\n")
    for word, name in zip(keywords, names):
        print(
            f'    {{ .word = "{name}",'
            f'{" " * (max_name - len(name))} '
            f'.type = {word:<{max_token}} }},'
        )
    print("}", end = "\n")

def print_token_to_string(keywords: list[str]):
    max_len = max(len(word) for word in keywords) + 4
    print("const char *token_to_string(enum TokenType token) {", end = "\n")
    print("\tswitch (token) {", end = "\n")
    for word in keywords:
        padded = f"{word:<{max_len}}"
        print(f"""\t\tcase {padded}: return "{word}";""", end = "\n")
    print("\t}", end = "\n")
    print("}", end = "\n")

def main():
    mode = get_mode()
    match mode:
        case "0":
            tokens = get_token_to_string_input()
            tokens = clean_input(tokens)
            tokens_list = input_to_list(tokens)
            print_token_to_string(tokens_list)

        case "1":
            tokens = get_reserved_words_input()
            tokens = clean_input(tokens)
            tokens_list = input_to_list(tokens)
            print_reserved_words(tokens_list)

        case _:
            print("Did not understand mode")


if __name__ == "__main__":
    main()