# Large test

```rust
fn main() {
    println!("hello");
}
```

Some text here.

```rust
pub trait PhaseItem: Sized + Send + Sync + 'static {
    const AUTOMATIC_BATCHING: bool = true;
    fn entity(&self) -> Entity;
    fn main_entity(&self) -> MainEntity;
    fn draw_function(&self) -> DrawFunctionId;
    fn batch_range(&self) -> &Range<u32>;
    fn batch_range_mut(&mut self) -> &mut Range<u32>;
    fn extra_index(&self) -> PhaseItemExtraIndex;
    fn batch_range_and_extra_index_mut(&mut self) -> (&mut Range<u32>, &mut PhaseItemExtraIndex);
}
```

More text.
