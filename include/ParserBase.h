#pragma once

#include "Buffer.h"
#include "BufferPool.h"
#include "Diagnostics.h"
#include "SourceLocation.h"
#include "SyntaxNode.h"
#include "Token.h"

namespace slang {

class Preprocessor;

// Base class for the Parser, which contains helpers and language-agnostic parsing routines.
// Mostly this helps keep the main Parser smaller and more focused.

class ParserBase {
protected:
    ParserBase(Preprocessor& preprocessor);

    SyntaxNode* prependTrivia(SyntaxNode* node, Trivia* trivia);
    Token prependTrivia(Token token, Trivia* trivia);

    void prependTrivia(SyntaxNode* node, Buffer<Trivia>& trivia);
    Token prependTrivia(Token token, Buffer<Trivia>& trivia);

    SyntaxNode* prependSkippedTokens(SyntaxNode* node, Buffer<Token>& tokens);
    Token prependSkippedTokens(Token node, Buffer<Token>& tokens);

    void reduceSkippedTokens(Buffer<Token>& skipped, Buffer<Trivia>& trivia);

    Diagnostics& getDiagnostics();
    Diagnostic& addError(DiagCode code, SourceLocation location);
    Token createExpectedToken(Token actual, TokenKind expected);

    Token peek(int offset);
    Token peek();
    bool peek(TokenKind kind);
    Token consume();
    Token consumeIf(TokenKind kind);
    Token expect(TokenKind kind);

    // sliding window of tokens
    class Window {
    public:
        explicit Window(Preprocessor& source) :
            tokenSource(source)
        {
            capacity = 32;
            buffer = new Token[capacity];
        }

        ~Window() { delete[] buffer; }

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        Preprocessor& tokenSource;
        Token* buffer = nullptr;
        Token currentToken;
        Token lastConsumed;
        int currentOffset = 0;
        int count = 0;
        int capacity = 0;

        void addNew();
        void moveToNext();
    };

    BumpAllocator& alloc;
    BufferPool<Trivia> triviaPool;
    BufferPool<Token> tokenPool;
    BufferPool<SyntaxNode*> nodePool;
    BufferPool<TokenOrSyntax> tosPool;

    enum class SkipAction {
        Continue,
        Abort
    };

    // this is a generalized method for parsing a delimiter separated list of things
    // with bookend tokens in a way that robustly handles bad tokens
    template<bool(*IsExpected)(TokenKind), bool(*IsEnd)(TokenKind), typename TParserFunc>
    void parseSeparatedList(
        TokenKind openKind,
        TokenKind closeKind,
        TokenKind separatorKind,
        Token& openToken,
        ArrayRef<TokenOrSyntax>& list,
        Token& closeToken,
        DiagCode code,
        TParserFunc&& parseItem
    ) {
        openToken = expect(openKind);

        auto buffer = tosPool.get();
        parseSeparatedList<IsExpected, IsEnd, TParserFunc>(buffer, closeKind, separatorKind, closeToken, code, std::forward<TParserFunc>(parseItem));
        list = buffer->copy(alloc);
    }

    template<bool(*IsExpected)(TokenKind), bool(*IsEnd)(TokenKind), typename TParserFunc>
    void parseSeparatedList(
        Buffer<TokenOrSyntax>& buffer,
        TokenKind closeKind,
        TokenKind separatorKind,
        Token& closeToken,
        DiagCode code,
        TParserFunc&& parseItem
    ) {
        Trivia skippedTokens;
        auto current = peek();
        if (!IsEnd(current.kind)) {
            while (true) {
                if (IsExpected(current.kind)) {
                    buffer.append(prependTrivia(parseItem(true), &skippedTokens));
                    while (true) {
                        current = peek();
                        if (IsEnd(current.kind))
                            break;

                        if (IsExpected(current.kind)) {
                            buffer.append(prependTrivia(expect(separatorKind), &skippedTokens));
                            buffer.append(prependTrivia(parseItem(false), &skippedTokens));
                            continue;
                        }

                        if (skipBadTokens<IsExpected, IsEnd>(&skippedTokens, code) == SkipAction::Abort)
                            break;
                    }
                    // found the end
                    break;
                }
                else if (skipBadTokens<IsExpected, IsEnd>(&skippedTokens, code) == SkipAction::Abort)
                    break;
                else
                    current = peek();
            }
        }
        closeToken = prependTrivia(expect(closeKind), &skippedTokens);
    }

    template<bool(*IsExpected)(TokenKind), bool(*IsAbort)(TokenKind)>
    SkipAction skipBadTokens(Trivia* skippedTokens, DiagCode code) {
        auto tokens = tokenPool.get();
        auto result = SkipAction::Continue;
        auto current = peek();
        bool error = false;

        while (!IsExpected(current.kind)) {
            if (!error) {
                addError(code, current.location());
                error = true;
            }

            if (current.kind == TokenKind::EndOfFile || IsAbort(current.kind)) {
                result = SkipAction::Abort;
                break;
            }
            tokens->append(consume());
            current = peek();
        }

        if (tokens->empty())
            *skippedTokens = Trivia();
        else
            *skippedTokens = Trivia(TriviaKind::SkippedTokens, tokens->copy(alloc));

        return result;
    }

    template<typename T>
    void prependTrivia(ArrayRef<T*> list, Trivia* trivia) {
        if (trivia->kind != TriviaKind::Unknown && !list.empty())
            prependTrivia(*list.begin(), trivia);
    }

private:
    Window window;
};

}