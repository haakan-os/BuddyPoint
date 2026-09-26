# Advanced maths on BuddyPoint

This note exercises notation from study notes. Original Markdown stays editable.

## Inline maths

_Point $A$ is the intersection of $y=x+1$ and $y=-3x+12$._ The coordinates are $(2.75,3.75)$, or $(\frac{11}{4},\frac{15}{4})$ as fractions. These expressions should share lines with the surrounding text and wrap naturally.

## Permutations

$$\begin{pmatrix}
n \\
r
\end{pmatrix}
\times r! = \frac{n!}{(n-r)!}
$$

$$\binom{8}{3}\times 3! = \frac{8!}{(8-3)!}=336$$

## Piecewise functions

$$g(x)=\begin{cases}
1-(x-1)^2 & \text{for x < 0} \\
e^{x^2} & \text{for x = 0} \\
0 & \text{for x > 0}
\end{cases}
$$

## Cancellation

$$\lim_{x \to4}\frac{(x+1)\cancel{(x-4)}}{(x-2)\cancel{(x-4)}}$$

$$\frac{-\cancel2x}{\cancel2\sqrt{4-x^2}}=\frac{-x}{\sqrt{4-x^2}}$$

## Adjacent equations and surrounding prose

$$-2=-m_2$$ $$m_2=2$$ $$\therefore y=2x+c$$

Arithmetic mean of the original class: $$\bar{x}_1:=\frac{h}{N}=170$$

$$x^2+5x-11=\pm3$$ gives two quadratic equations.

$$x=\frac{-5\pm\sqrt{5^2-4\times1\times(-8)}}{2\times1}$$$$x=\frac{-5\pm\sqrt{57}}{2}$$

## Long equations

$$\frac{dy}{du}=\frac{1}{2}(u)^{\frac{1}{2}-1}=\frac{1}{2}\times\frac{1}{(u)^{\frac{1}{2}}}=\frac{1}{2\sqrt{u}}=\frac{1}{2\sqrt{4-x^2}}$$

## Code and currency stay literal

These examples must not turn into equations:

```tex
$$\begin{pmatrix}n\\r\end{pmatrix}$$
```

The inline code `$x^2$` stays literal. Prices of $5 and $10 stay text.

- [ ] Check the matrix, cases and crossed-out factors.
- [ ] Check text and inline maths share the same line.
- [ ] Check no equations are replaced by empty boxes.
